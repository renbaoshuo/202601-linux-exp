#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "chat.h"
#include "common.h"
#include "httplib.h"
#include "llama.h"
#include "log.h"

using json = common_json;

constexpr int CLIENTS = 2;
constexpr int CONTEXT_SIZE = 65536;
constexpr int BATCH_SIZE = 512;

void record(std::ofstream &log, const json &event) {
  log << event.dump_safe() << '\n'
      << std::flush;
  if (!log) {
    throw std::runtime_error("Cannot write request log");
  }
}

bool send_event(httplib::DataSink &sink, std::ofstream &log, const json &event) {
  record(log, event);

  const auto data = "data: " + event.dump_safe() + "\n\n";
  return sink.write(data.data(), data.size());
}

json chunk(const json &delta, const json &finish = nullptr) {
  return {{"choices", json::array({json{{"index", 0}, {"delta", delta}, {"finish_reason", finish}}})}};
}

bool complete_utf8(const std::string &text) {
  if (text.empty()) {
    return true;
  }

  // 定位末尾字符的起始字节。
  size_t start = text.size() - 1;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) {
    --start;
  }

  // 检查末尾字符的字节是否完整。
  const auto lead = static_cast<unsigned char>(text[start]);
  const size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2
                                    : lead < 0xf0     ? 3
                                                      : 4;
  return text.size() - start >= length;
}

bool generate(llama_model *model, const json &body, const std::string &request_id, httplib::DataSink &sink, std::ofstream &log) {
  // 加载聊天模板，将消息和工具转换为提示词。
  auto templates = common_chat_templates_init(model, "");
  common_chat_templates_inputs inputs;
  inputs.messages = common_chat_msgs_parse_oaicompat(body.at("messages"));
  inputs.tools = common_chat_tools_parse_oaicompat(body.at("tools"));
  inputs.enable_thinking = false;
  inputs.reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK;
  inputs.chat_template_kwargs["enable_thinking"] = "false";
  const auto chat = common_chat_templates_apply(templates.get(), inputs);

  // 准备输出解析规则，并对提示词分词。
  common_chat_parser_params parser(chat);
  parser.reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK;
  parser.parser.load(chat.parser);

  const auto *vocab = llama_model_get_vocab(model);
  auto tokens = common_tokenize(vocab, chat.prompt, true, true);

  // 校验提示词与生成长度是否符合上下文限制。
  const int max_tokens = body.at("max_tokens").get<int>();
  if (tokens.empty()) {
    throw std::runtime_error("Empty prompt");
  }
  if (tokens.size() + max_tokens > CONTEXT_SIZE) {
    if (!send_event(sink, log, {{"error", json{{"code", "context_length_exceeded"}, {"message", "Prompt plus max_tokens exceeds the " + std::to_string(CONTEXT_SIZE) + "-token context"}}}})) {
      return false;
    }
    sink.done();
    return true;
  }

  // 创建本次请求的推理上下文和采样器，退出时自动释放。
  auto params = llama_context_default_params();
  params.n_ctx = CONTEXT_SIZE;
  params.n_batch = BATCH_SIZE;
  params.n_ubatch = BATCH_SIZE;
  params.n_seq_max = 1;
  params.n_threads = 2;
  params.n_threads_batch = 2;

  std::unique_ptr<llama_context, decltype(&llama_free)> context(
      llama_init_from_model(model, params), llama_free);
  std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler(
      llama_sampler_init_greedy(), llama_sampler_free);
  if (!context || !sampler) {
    throw std::runtime_error("Cannot create inference context or sampler");
  }

  // 分批处理提示词，完成推理预填充。
  for (size_t offset = 0; offset < tokens.size(); offset += BATCH_SIZE) {
    const int count = static_cast<int>(std::min<size_t>(BATCH_SIZE, tokens.size() - offset));
    if (!sink.is_writable()) {
      return false;
    }

    if (llama_decode(context.get(), llama_batch_get_one(tokens.data() + offset, count)) != 0) {
      throw std::runtime_error("Prompt evaluation failed");
    }

    if (!sink.write(": prefill\n\n", 11)) {
      return false;
    }
  }

  // 解析生成内容，只发送新增的正文。
  std::string generated, sent_content;
  common_chat_msg answer;
  bool stopped = false;
  auto send_content = [&](bool partial) {
    answer = common_chat_parse(generated, partial, parser);
    if (answer.content.size() <= sent_content.size()) {
      return true;
    }
    if (answer.content.compare(0, sent_content.size(), sent_content) != 0) {
      throw std::runtime_error("Model parser revised already streamed content");
    }

    const auto delta = answer.content.substr(sent_content.size());
    sent_content = answer.content;
    return send_event(sink, log, chunk({{"content", delta}}));
  };

  // 逐个生成 token，并在 UTF-8 字符完整后发送正文。
  for (int i = 0; i < max_tokens; ++i) {
    if (!sink.is_writable()) {
      return false;
    }

    llama_token token = llama_sampler_sample(sampler.get(), context.get(), -1);
    if (llama_vocab_is_eog(vocab, token)) {
      stopped = true;
      break;
    }

    generated += common_token_to_piece(vocab, token, true);
    if (complete_utf8(generated) && !send_content(true)) {
      return false;
    }
    if (!sink.write(": token\n\n", 9)) {
      return false;
    }

    if (i + 1 < max_tokens && llama_decode(context.get(), llama_batch_get_one(&token, 1)) != 0) {
      throw std::runtime_error("Token evaluation failed");
    }
  }

  // 根据结束原因发送最终正文、工具调用和结束状态。
  if (!stopped) {
    if (!send_event(sink, log, chunk(json::object(), "length"))) {
      return false;
    }
  } else {
    if (!complete_utf8(generated)) {
      throw std::runtime_error("Incomplete UTF-8 in model output");
    }

    if (!send_content(false)) {
      return false;
    }

    json calls = json::array();
    for (size_t i = 0; i < answer.tool_calls.size(); ++i) {
      const auto &call = answer.tool_calls[i];
      if (call.name.empty() || !json::parse(call.arguments).is_object()) {
        throw std::runtime_error("Invalid model tool call");
      }

      calls.push_back({{"index", static_cast<int>(i)}, {"type", "function"}, {"id", request_id + "_" + std::to_string(i)}, {"function", json{{"name", call.name}, {"arguments", call.arguments}}}});
    }

    if (!calls.empty() && !send_event(sink, log, chunk({{"tool_calls", calls}}))) {
      return false;
    }
    if (!send_event(sink, log, chunk(json::object(), calls.empty() ? "stop" : "tool_calls"))) {
      return false;
    }
  }

  // 发送流结束标记并完成响应。
  if (!sink.write("data: [DONE]\n\n", 14)) {
    return false;
  }
  sink.done();
  return true;
}

bool serve_request(llama_model *model, const json &body, httplib::DataSink &sink) {
  std::ofstream log;
  try {
    // 创建本次请求独立的日志文件。
    char path[] = ".fucode-server/request-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) {
      throw std::runtime_error("Cannot create request log");
    }
    close(fd);
    log.open(path);

    record(log, {{"event", "request"}, {"body", body}});

    const bool ok = generate(model, body, std::filesystem::path(path).filename().string(), sink, log);

    // 记录结束状态并返回处理结果。
    record(log, {{"event", ok ? "finished" : "disconnected"}});
    return ok;
  } catch (const std::exception &e) {
    // 将异常写入请求日志。
    const json error = {{"error", json{{"message", e.what()}}}};
    if (log.is_open()) {
      log << error.dump_safe() << '\n'
          << std::flush;
    }

    // 向客户端返回错误并结束响应。
    const auto data = "data: " + error.dump_safe() + "\n\n";
    if (!sink.write(data.data(), data.size())) {
      return false;
    }
    sink.done();
    return true;
  }
}

int run(const char *model_path) {
  // 加载模型并检查聊天模板。
  std::cerr << "Loading model: " << model_path << '\n';
  auto params = llama_model_default_params();
  params.n_gpu_layers = 0;
  std::unique_ptr<llama_model, decltype(&llama_model_free)> model(
      llama_model_load_from_file(model_path, params), llama_model_free);
  if (!model) {
    throw std::runtime_error("Cannot load GGUF model");
  }
  if (!llama_model_chat_template(model.get(), nullptr)) {
    throw std::runtime_error("GGUF model has no chat template");
  }

  // 准备日志目录与 HTTP 服务。
  std::filesystem::create_directory(".fucode-server");
  httplib::Server server;
  server.new_task_queue = [] { return new httplib::ThreadPool(CLIENTS); };
  server.set_payload_max_length(1024 * 1024);

  server.Post("/v1/chat/completions", [&](const httplib::Request &req, httplib::Response &res) {
    try {
      // 解析并校验请求参数。
      const auto body = json::parse(req.body);
      if (body.at("model").get<std::string>() != "Qwen3.5-9B" || !body.at("stream").get<bool>() || !body.at("messages").is_array() || body.at("messages").empty() || !body.at("tools").is_array() || !body.at("max_tokens").is_number_integer() || body.at("max_tokens").get<int>() < 1 || body.at("max_tokens").get<int>() > 2048) {
        throw std::runtime_error("Expected a streaming Qwen3.5-9B request with messages, tools and max_tokens (1-2048)");
      }

      // 配置 SSE 响应，交给请求处理函数生成内容。
      res.set_header("Cache-Control", "no-cache");
      res.set_chunked_content_provider("text/event-stream",
                                       [model = model.get(), body](size_t, httplib::DataSink &sink) {
                                         return serve_request(model, body, sink);
                                       });
    } catch (const std::exception &e) {
      res.status = 400;
      res.set_content(json{{"error", json{{"message", e.what()}}}}.dump_safe(), "application/json");
    }
  });

  // 绑定端口并进入请求处理循环。
  if (!server.bind_to_port("127.0.0.1", 8080)) {
    throw std::runtime_error("Cannot bind 127.0.0.1:8080");
  }
  std::cerr << "Model loaded. Listening on http://127.0.0.1:8080 (2 workers, CPU)\n";
  return server.listen_after_bind() ? 0 : 1;
}

int main(int argc, char **argv) {
  // 校验模型路径参数。
  if (argc != 2) {
    std::cerr << "Usage: fucode-server /path/to/Qwen3.5-9B.gguf\n";
    return 1;
  }

  // 初始化信号处理、日志与推理后端。
  std::signal(SIGPIPE, SIG_IGN);
  common_log_set_verbosity_thold(-1);
  llama_backend_init();
  ggml_backend_load_all();

  // 启动服务并记录退出状态。
  int status = 1;
  try {
    status = run(argv[1]);
  } catch (const std::exception &e) {
    std::cerr << "fucode-server: " << e.what() << '\n';
  }

  // 释放全局后端资源。
  llama_backend_free();

  return status;
}

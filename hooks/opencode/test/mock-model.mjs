// A tiny OpenAI-compatible model server for tests: streams scripted replies so OpenCode runs a real
// turn (thinking, a tool call, a reply) without a real model. Usage: startMockModel() → { url, close, requests }.
import { createServer } from "node:http";

function sse(res, chunks) {
  res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache" });
  for (const c of chunks) res.write(`data: ${JSON.stringify(c)}\n\n`);
  res.end("data: [DONE]\n\n");
}

const chunk = (delta, finish = null) => ({ id: "c1", object: "chat.completion.chunk", created: 1, model: "mock", choices: [{ index: 0, delta, finish_reason: finish }] });
const usage = { id: "c1", object: "chat.completion.chunk", created: 1, model: "mock", choices: [], usage: { prompt_tokens: 1200, completion_tokens: 20, total_tokens: 1220 } };

function textOf(m) {
  if (typeof m?.content === "string") return m.content;
  return (m?.content || []).map((p) => p.text || "").join(" ");
}

export function startMockModel(port = 0) {
  const requests = [];
  const server = createServer((req, res) => {
    let body = "";
    req.on("data", (d) => (body += d));
    req.on("end", () => {
      if (req.url.endsWith("/models")) {
        res.writeHead(200, { "content-type": "application/json" });
        return res.end(JSON.stringify({ data: [{ id: "mock-coder", object: "model" }] }));
      }
      const b = JSON.parse(body || "{}");
      requests.push(b);
      const msgs = b.messages || [];
      const last = msgs[msgs.length - 1];
      const lastUser = [...msgs].reverse().find((m) => m.role === "user");
      const words = (s) => s.split(/(?<= )/).map((w) => chunk({ content: w }));
      // No tools: OpenCode asking for a title or a summary.
      if (!b.tools?.length) return sse(res, [chunk({ role: "assistant", content: "Mock session" }), chunk({}, "stop"), usage]);
      if (last?.role === "tool" && !/They answered|Shown on Deskling|planned|No answer/i.test(textOf(last)))
        return sse(res, [chunk({ role: "assistant" }), ...words("The command printed hi, so the shell works."), chunk({}, "stop"), usage]);
      const call = (name, args, say) =>
        sse(res, [
          chunk({ role: "assistant", content: say }),
          chunk({ tool_calls: [{ index: 0, id: "call_" + name + "_" + requests.length, type: "function", function: { name, arguments: JSON.stringify(args) } }] }),
          chunk({}, "tool_calls"),
          usage,
        ]);
      const has = (name) => (b.tools || []).some((t) => t.function?.name === name);
      if (last?.role === "tool" && /They answered|Shown on Deskling|planned|No answer/i.test(textOf(last)))
        return sse(res, [chunk({ role: "assistant" }), ...words("Okay. " + textOf(last).slice(0, 60)), chunk({}, "stop"), usage]);
      if (/notify me/i.test(textOf(lastUser)) && has("deskling_notify")) return call("deskling_notify", { title: "Tests pass", body: "All 12 green" }, "Letting you know.");
      if (/ask me/i.test(textOf(lastUser)) && has("deskling_ask")) return call("deskling_ask", { question: "Which database?", options: ["Postgres", "SQLite"] }, "");
      if (/make a plan/i.test(textOf(lastUser)) && has("task"))
        return call("task", { description: "Plan the change", prompt: "Plan adding a dark mode. Reply with: planned.", subagent_type: "deskling-planner" }, "Asking the planner.");
      if (/Reply with: planned/.test(textOf(lastUser))) return sse(res, [chunk({ role: "assistant" }), ...words("planned: 1. add a toggle 2. add styles"), chunk({}, "stop"), usage]);
      if (/run/i.test(textOf(lastUser))) {
        return sse(res, [
          chunk({ role: "assistant", reasoning_content: "The user wants a command run.\nI'll use bash." }),
          chunk({ content: "Running it now." }),
          chunk({ tool_calls: [{ index: 0, id: "call_1", type: "function", function: { name: "bash", arguments: "" } }] }),
          chunk({ tool_calls: [{ index: 0, function: { arguments: JSON.stringify({ command: "echo hi", description: "Say hi" }) } }] }),
          chunk({}, "tool_calls"),
          usage,
        ]);
      }
      return sse(res, [chunk({ role: "assistant" }), ...words("Hello from the local model. " + textOf(lastUser).slice(0, 40)), chunk({}, "stop"), usage]);
    });
  });
  return new Promise((resolve) =>
    server.listen(port, "127.0.0.1", () => resolve({ url: `http://127.0.0.1:${server.address().port}/v1`, requests, close: () => server.close() })),
  );
}

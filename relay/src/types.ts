export type AgentState = "idle" | "working" | "waiting" | "needs_you" | "done";

/** Which tool the session lives in, so the screen can badge it. */
export type Source = "claude-code" | "cursor" | "codex" | "gemini" | "custom";

export interface Agent {
  id: string; // Claude Code session_id, Cursor conversation_id, or a custom id
  source: Source;
  name: string; // project folder name or a custom label
  host: string; // machine name
  os: string; // mac | windows | linux | cloud | ...
  state: AgentState;
  task: string; // one line: what it's doing right now
  lastPrompt: string; // the last thing you asked it
  lastMessage: string; // the last thing it said
  since: number; // ms epoch when the current state started
  updated: number;
  helpers?: Helper[]; // subagents it has handed work to
  listenUntil?: number; // "Keep listening" from the screen: when it finishes, it waits for you until then
  streaming?: boolean; // its transcript reaches the relay, so words, thoughts and steps come from there
  model?: string; // the model of its latest reply, e.g. "claude-opus-5-5"
  context?: number; // tokens in its context window as of its latest reply
  quotes?: Record<string, string>; // excerpts you replied to, by their file name
  helperNames?: string[]; // descriptions from Agent tool calls, waiting for their SubagentStart
}

/** A subagent working for a session. */
export interface Helper {
  id: string; // Claude Code's agent_id
  name: string; // what it was asked to do
  task: string; // what it's doing now
  done: boolean;
  updated: number;
}

export type FeedLevel = "info" | "good" | "alert" | "reminder";

export interface FeedItem {
  id: string;
  level: FeedLevel;
  title: string;
  body: string;
  agentId: string | null;
  ts: number;
}

export interface Reminder {
  id: string;
  title: string;
  due: number | null; // ms epoch, or null for "whenever"
  from: string; // who set it: "Claude", "scheduled task", "you"...
  done: boolean;
  doneAt: number | null;
  notified: boolean;
}

export interface QuestionOption {
  label: string;
  description: string;
}

export interface Question {
  question: string;
  header: string;
  options: QuestionOption[];
  multiSelect: boolean;
}

export interface Pending {
  id: string;
  kind: "permission" | "question";
  agentId: string;
  source: Source;
  name: string;
  host: string;
  os: string;
  tool: string;
  summary: string; // the command or file the tool wants to touch
  detail: string; // short explanation if we have one
  questions: Question[]; // for kind === "question"
  ts: number;
  expires: number;
}

export interface ChatLine {
  id: string;
  from: "you" | "agent" | "tool" | "thought"; // "tool" = a step it took, "thought" = a line of its thinking
  image?: string; // id of a picture in the chat (GET /img/<id>)
  quote?: string; // the bit of Claude's message you were replying to
  text: string;
  status: "queued" | "delivered" | "sent" | "typed"; // "sent" = an agent reply, "typed" = you, on the computer
  ts: number;
}

export interface State {
  agents: Record<string, Agent>;
  feed: FeedItem[];
  reminders: Reminder[];
  pending: Pending[];
  chats: Record<string, ChatLine[]>; // per agent id
  focus: boolean;
  pingWhenDone: boolean;
}

export type Decision = "allow" | "deny" | "computer";

/** Answers keyed by question text: a label, or labels for multi-select. */
export type Answers = Record<string, string | string[]>;

/** Why a snapshot was sent, so the screen can pick an animation. */
export interface Cause {
  kind:
    | "hello"
    | "agent"
    | "feed"
    | "feed_removed"
    | "permission"
    | "question"
    | "permission_closed"
    | "done"
    | "ping"
    | "chat"
    | "reminder"
    | "reminder_due"
    | "reminder_done"
    | "focus";
  id?: string;
}

export type AgentState = "idle" | "working" | "waiting" | "needs_you" | "done";

export interface Agent {
  id: string; // Claude Code session_id, or a custom id for other agents
  name: string; // project folder name or a custom label
  host: string; // machine name
  os: string; // mac | windows | linux | cloud | ...
  state: AgentState;
  task: string; // one-line description of what it's doing
  since: number; // ms epoch when the current state started
  updated: number;
}

export type FeedLevel = "info" | "good" | "alert" | "reminder";

export interface FeedItem {
  id: string;
  level: FeedLevel;
  title: string;
  body: string;
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

export interface Pending {
  id: string;
  agentId: string;
  name: string;
  host: string;
  os: string;
  tool: string;
  summary: string; // the command or file the tool wants to touch
  detail: string; // short explanation if we have one
  ts: number;
  expires: number;
}

export interface State {
  agents: Record<string, Agent>;
  feed: FeedItem[];
  reminders: Reminder[];
  pending: Pending[];
  focus: boolean;
  pingWhenDone: boolean;
}

export type Decision = "allow" | "deny" | "computer";

/** Why a snapshot was sent, so the screen can pick an animation. */
export interface Cause {
  kind:
    | "hello"
    | "agent"
    | "feed"
    | "permission"
    | "permission_closed"
    | "done"
    | "ping"
    | "reminder"
    | "reminder_due"
    | "reminder_done"
    | "focus";
  id?: string;
}

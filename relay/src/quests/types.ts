// Quests: your work from Mojo Desk, Linear and Jira, as one list the screen can play with.

export type Source = "mojo" | "linear" | "jira";
export type TaskStatus = "todo" | "progress" | "review" | "done";

/** One ticket, whatever it came from. */
export interface Task {
  id: string; // "<source>:<its own id>", unique across sources
  src: Source;
  key: string; // what people call it: DKL-30, ENG-12, ABC-7
  title: string;
  status: TaskStatus;
  priority: 0 | 1 | 2 | 3 | 4; // none, low, medium, high, urgent
  estMin: number | null;
  due: string | null; // YYYY-MM-DD
  url: string;
  project: string;
  doneAt?: number; // when it was finished (from the service, or when we saw it)
  spentMin?: number; // focus minutes put into it from Deskling (a boss's damage)
}

/** What you connected. Kept apart from the state and never sent back out. */
export interface Credentials {
  mojo?: { url: string; token: string };
  linear?: { token: string; url?: string };
  jira?: { site: string; email: string; token: string };
}

export interface Connection {
  kind: Source;
  account: string; // who the key belongs to, to show it's the right one
  error: string; // the latest sync's problem, "" when fine
  syncedAt: number;
  since?: number; // connected at: finishes before this never pay
}

export interface Challenge {
  id: "finish" | "boss" | "focus" | "clear";
  label: string;
  goal: number;
  have: number;
  done: boolean;
}

export type GameEventKind = "xp" | "level" | "combo" | "boss" | "cleared" | "challenge" | "unlock" | "streak" | "focus";

/** Something to celebrate on the screen (each screen plays the ones it hasn't seen). */
export interface GameEvent {
  seq: number;
  kind: GameEventKind;
  amount: number;
  label: string;
  ts: number;
}

export interface Game {
  xp: number;
  streak: number; // working days in a row with something finished
  best: number;
  lastDay: string; // the last day something was finished
  today: { date: string; done: number; xp: number; cleared: boolean };
  combo: { n: number; until: number; at?: number }; // at: when the last ticket in it was finished
  week: { start: string; challenges: Challenge[] };
  awarded: string[]; // tasks already rewarded (finishing twice doesn't pay twice)
  unlocks: string[];
  focusMin: number;
  bosses: number;
  seq: number;
  events: GameEvent[];
}

export interface Focus {
  id: string; // task
  startedAt: number;
  endsAt: number;
  minutes: number;
}

export interface Quests {
  tz: string;
  tasks: Task[];
  plan: { date: string; ids: string[]; auto: boolean };
  snoozed: Record<string, string>; // task id -> the day it comes back
  focus: Focus | null;
  connections: Connection[];
  game: Game;
  syncAt: number; // the next sync
}

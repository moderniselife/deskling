// The quest book: tickets from every service in one list, today's quests, focus, and the game.
// No I/O here (the hub talks to the services), so it's tested on its own.

import { bossHp, emptyGame, finish, focused, isBoss, localDate, rollover, view as gameView } from "./game";
import type { Quests, Source, Task, TaskStatus } from "./types";

export const PLAN_AUTO = 5; // quests picked for you when a day starts
export const PLAN_MAX = 12;
const PER_SOURCE = 40; // open tickets kept per service (the best-ranked; planned and focused ones always)
const DONE_KEPT = 30;

export function emptyQuests(tz = "Australia/Sydney"): Quests {
  return { tz, tasks: [], plan: { date: "", ids: [], auto: true }, snoozed: {}, focus: null, connections: [], game: emptyGame(), syncAt: 0 };
}

const today = (q: Quests, now: number) => localDate(now, q.tz);

function rank(t: Task, day: string): number {
  // In progress first, then bosses, then priority, then what's due soonest.
  const st = { progress: 0, review: 2, todo: 1, done: 3 }[t.status];
  return st * 1000 + (isBoss(t, day) ? 0 : 100) + (4 - t.priority) * 10 + (t.due ? Math.min(9, Math.max(0, Math.round((Date.parse(t.due) - Date.parse(day)) / 864e5))) : 9);
}

/** Today's plan exists (a new day starts with the top few open quests, which you can change). */
export function ensurePlan(q: Quests, now: number): void {
  const day = today(q, now);
  rollover(q.game, now, q.tz);
  for (const [id, back] of Object.entries(q.snoozed)) if (back <= day) delete q.snoozed[id];
  if (q.plan.date === day) {
    // Drop quests that vanished (reassigned, deleted).
    q.plan.ids = q.plan.ids.filter((id) => q.tasks.some((t) => t.id === id));
    // A plan made for you fills up as other services' tickets arrive (one you edited is left alone).
    if (q.plan.auto && q.plan.ids.length < PLAN_AUTO) {
      const more = q.tasks
        .filter((t) => t.status !== "done" && !q.snoozed[t.id] && !q.plan.ids.includes(t.id))
        .sort((a, b) => rank(a, day) - rank(b, day));
      q.plan.ids.push(...more.slice(0, PLAN_AUTO - q.plan.ids.length).map((t) => t.id));
    }
    return;
  }
  const open = q.tasks.filter((t) => t.status !== "done" && !q.snoozed[t.id]).sort((a, b) => rank(a, day) - rank(b, day));
  q.plan = { date: day, ids: open.slice(0, PLAN_AUTO).map((t) => t.id), auto: true };
}

function planDone(q: Quests): boolean {
  const ids = q.plan.ids;
  return ids.length > 0 && ids.every((id) => q.tasks.find((t) => t.id === id)?.status === "done");
}

/** A ticket was finished (here or elsewhere): pay out once. */
function finished(q: Quests, t: Task, now: number): number {
  return finish(q.game, t, { now, tz: q.tz, planned: q.plan.ids.includes(t.id), allPlannedDone: planDone(q) });
}

/**
 * Replace one service's tickets with a fresh list. Tickets that became done since the last look
 * earn XP. The first sync of a service only learns what's there: what was already done pays nothing.
 */
export function merge(q: Quests, src: Source, fresh: Task[], now: number, first: boolean, since = 0): number {
  const before = new Map(q.tasks.filter((t) => t.src === src).map((t) => [t.id, t]));
  const kept = q.tasks.filter((t) => t.src !== src);
  const day = today(q, now);
  const keep = (t: Task) => q.plan.ids.includes(t.id) || q.focus?.id === t.id;
  let next = fresh.map((t) => {
    const was = before.get(t.id);
    return { ...t, ...(was?.spentMin ? { spentMin: was.spentMin } : {}) };
  });
  // Bounded per service: the best-ranked open tickets (always the planned and focused ones), and
  // the latest finished.
  const open = next.filter((t) => t.status !== "done").sort((a, b) => Number(keep(b)) - Number(keep(a)) || rank(a, day) - rank(b, day));
  const done = next.filter((t) => t.status === "done").sort((a, b) => (b.doneAt ?? 0) - (a.doneAt ?? 0));
  next = [...open.slice(0, Math.max(PER_SOURCE, open.filter(keep).length)), ...done.slice(0, DONE_KEPT)];
  q.tasks = [...kept, ...next];
  ensurePlan(q, now);
  let xp = 0;
  for (const t of next) {
    if (t.status !== "done") continue;
    const was = before.get(t.id);
    // It pays when we saw it open and now it's done, or when the service says it was finished after
    // you connected. Never on the first look, and never for what was done before.
    const seenOpen = !!was && was.status !== "done";
    const finishedSince = !!t.doneAt && since > 0 && t.doneAt >= since && !was;
    if (first || !(seenOpen || finishedSince)) {
      if (!q.game.awarded.includes(t.id)) q.game.awarded.push(t.id);
      if (q.game.awarded.length > 500) q.game.awarded.splice(0, q.game.awarded.length - 500);
      continue;
    }
    xp += finished(q, t, now);
  }
  return xp;
}

/** You moved a ticket on a screen (the service has already accepted it). */
export function moved(q: Quests, id: string, to: TaskStatus, now: number): number {
  const t = q.tasks.find((x) => x.id === id);
  if (!t) return 0;
  const wasDone = t.status === "done";
  t.status = to;
  if (to === "done") {
    t.doneAt = now;
    if (!wasDone) return finished(q, t, now);
  }
  return 0;
}

export function setPlanned(q: Quests, id: string, on: boolean, now: number): void {
  ensurePlan(q, now);
  if (!q.tasks.some((t) => t.id === id)) return;
  q.plan.auto = false;
  q.plan.ids = q.plan.ids.filter((x) => x !== id);
  if (on && q.plan.ids.length < PLAN_MAX) q.plan.ids.push(id);
  if (on) delete q.snoozed[id];
}

/** Move a quest up or down today's list. */
export function reorder(q: Quests, id: string, by: number, now: number): void {
  ensurePlan(q, now);
  const i = q.plan.ids.indexOf(id);
  if (i < 0) return;
  const j = Math.max(0, Math.min(q.plan.ids.length - 1, i + by));
  q.plan.ids.splice(i, 1);
  q.plan.ids.splice(j, 0, id);
  q.plan.auto = false;
}

/** Not today: off the plan and out of the suggestions until tomorrow. */
export function snooze(q: Quests, id: string, now: number): void {
  ensurePlan(q, now);
  if (!q.tasks.some((t) => t.id === id)) return;
  const day = today(q, now);
  const d = new Date(day + "T12:00:00Z");
  d.setUTCDate(d.getUTCDate() + 1);
  q.snoozed[id] = d.toISOString().slice(0, 10);
  q.plan.ids = q.plan.ids.filter((x) => x !== id);
}

export function startFocus(q: Quests, id: string, minutes: number, now: number): boolean {
  if (!q.tasks.some((t) => t.id === id)) return false;
  const m = Math.max(5, Math.min(90, Math.round(minutes) || 25));
  q.focus = { id, startedAt: now, endsAt: now + m * 60000, minutes: m };
  return true;
}

/** End the focus block (at its time, or early). Returns the minutes it counted and the task. */
export function endFocus(q: Quests, now: number): { minutes: number; task: Task | null; startedAt: number } {
  const f = q.focus;
  q.focus = null;
  if (!f) return { minutes: 0, task: null, startedAt: 0 };
  const minutes = Math.round((Math.min(now, f.endsAt) - f.startedAt) / 60000);
  const t = q.tasks.find((x) => x.id === f.id) ?? null;
  if (t && minutes >= 5) t.spentMin = (t.spentMin ?? 0) + minutes;
  focused(q.game, minutes, t ? `Focused on ${t.key}` : "Focus", now, q.tz);
  return { minutes, task: t, startedAt: f.startedAt };
}

/** What every screen gets: compact, open quests first, today's in plan order. */
export function questsView(q: Quests, now: number) {
  ensurePlan(q, now);
  const day = today(q, now);
  const planned = q.plan.ids.map((id) => q.tasks.find((t) => t.id === id)).filter((t): t is Task => !!t);
  const others = q.tasks
    .filter((t) => !q.plan.ids.includes(t.id) && t.status !== "done")
    .sort((a, b) => rank(a, day) - rank(b, day))
    .slice(0, 24);
  const row = (t: Task) => {
    const boss = isBoss(t, day);
    const hp = boss ? bossHp(t) : null;
    return {
      id: t.id,
      src: t.src,
      key: t.key,
      title: t.title.slice(0, 120),
      status: t.status,
      prio: t.priority,
      est: t.estMin ?? 0,
      due: t.due ?? "",
      overdue: !!t.due && t.due < day && t.status !== "done",
      planned: q.plan.ids.includes(t.id),
      snoozed: !!q.snoozed[t.id],
      boss,
      hp: hp?.hp ?? 0,
      hpMax: hp?.max ?? 0,
      url: t.url,
    };
  };
  return {
    connected: q.connections.map((c) => ({ kind: c.kind, account: c.account, error: c.error, syncedAt: c.syncedAt })),
    date: day,
    tasks: [...planned, ...others].map(row),
    planDone: planned.filter((t) => t.status === "done").length,
    planTotal: planned.length,
    focus: q.focus ? { id: q.focus.id, startedAt: q.focus.startedAt, endsAt: q.focus.endsAt, minutes: q.focus.minutes } : null,
    game: gameView(q.game, now, q.tz),
  };
}

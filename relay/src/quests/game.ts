// The game on top of your real work. Pure functions over the Game state, so the rules can be
// tested on their own and every screen sees the same score.
//
// XP for a finished ticket: priority (urgent 50, high 35, medium 25, low/none 15), plus 1 per 15
// estimated minutes (up to 8 h), x1.5 if it was one of today's quests, x2 for a boss, and a combo
// bonus of +10% per ticket finished within 45 minutes of the last one (up to +50%).
// Bosses: urgent, estimated at 4 h or more, or overdue. Focus minutes on one are its damage.
// Levels: level n starts at 50·n·(n−1) XP (100, 300, 600, 1000…). Streak: working days in a row
// with something finished (weekends don't break it). Clearing every quest of the day: +100.
// Each week (from Monday) has four challenges worth 150 XP each.

import type { Challenge, Game, GameEvent, GameEventKind, Task } from "./types";

export const COMBO_MS = 45 * 60 * 1000;
export const UNLOCKS: { level: number; id: string; label: string }[] = [
  { level: 2, id: "scarf", label: "a cosy scarf" },
  { level: 3, id: "glasses", label: "smart glasses" },
  { level: 5, id: "headphones", label: "focus headphones" },
  { level: 7, id: "party-hat", label: "a party hat" },
  { level: 10, id: "crown", label: "the crown" },
];

const PRIORITY_XP = [15, 15, 25, 35, 50];

export function emptyGame(): Game {
  return {
    xp: 0,
    streak: 0,
    best: 0,
    lastDay: "",
    today: { date: "", done: 0, xp: 0, cleared: false },
    combo: { n: 0, until: 0 },
    week: { start: "", challenges: [] },
    awarded: [],
    unlocks: [],
    focusMin: 0,
    bosses: 0,
    seq: 0,
    events: [],
  };
}

/** YYYY-MM-DD in a time zone. */
export function localDate(ts: number, tz: string): string {
  try {
    return new Intl.DateTimeFormat("en-CA", { timeZone: tz, year: "numeric", month: "2-digit", day: "2-digit" }).format(ts);
  } catch {
    return new Date(ts).toISOString().slice(0, 10);
  }
}

function addDays(day: string, n: number): string {
  const d = new Date(day + "T12:00:00Z");
  d.setUTCDate(d.getUTCDate() + n);
  return d.toISOString().slice(0, 10);
}

function weekday(day: string): number {
  return new Date(day + "T12:00:00Z").getUTCDay(); // 0 Sunday … 6 Saturday
}

/** The working day before `day` (Friday before a Monday). */
export function previousWorkday(day: string): string {
  let d = addDays(day, -1);
  while (weekday(d) === 0 || weekday(d) === 6) d = addDays(d, -1);
  return d;
}

export function weekStart(day: string): string {
  return addDays(day, -((weekday(day) + 6) % 7));
}

export function levelStart(n: number): number {
  return 50 * n * (n - 1);
}

export function levelFor(xp: number): number {
  let n = 1;
  while (xp >= levelStart(n + 1)) n++;
  return n;
}

export function isBoss(t: Task, today: string): boolean {
  return t.priority === 4 || (t.estMin ?? 0) >= 240 || (!!t.due && t.due < today && t.status !== "done");
}

/** Was it a boss while it was open? (Finishing an overdue ticket is still defeating a boss.) */
function wasBoss(t: Task, day: string): boolean {
  return isBoss({ ...t, status: "todo" }, day);
}

/** A boss's health: its estimate (an hour at least), less the focus minutes put into it. */
export function bossHp(t: Task): { hp: number; max: number } {
  const max = Math.max(60, t.estMin ?? 0);
  return { hp: Math.max(0, max - (t.spentMin ?? 0)), max };
}

function freshChallenges(): Challenge[] {
  return [
    { id: "finish", label: "Finish 10 quests", goal: 10, have: 0, done: false },
    { id: "boss", label: "Defeat 2 bosses", goal: 2, have: 0, done: false },
    { id: "focus", label: "Focus for 5 hours", goal: 300, have: 0, done: false },
    { id: "clear", label: "Clear 3 days", goal: 3, have: 0, done: false },
  ];
}

function event(g: Game, kind: GameEventKind, amount: number, label: string, now: number): void {
  g.seq++;
  g.events.push({ seq: g.seq, kind, amount, label, ts: now });
  if (g.events.length > 20) g.events.splice(0, g.events.length - 20);
}

function gain(g: Game, xp: number, now: number): void {
  const before = levelFor(g.xp);
  g.xp += xp;
  g.today.xp += xp;
  const after = levelFor(g.xp);
  if (after > before) {
    event(g, "level", after, `Level ${after}`, now);
    for (const u of UNLOCKS)
      if (u.level > before && u.level <= after && !g.unlocks.includes(u.id)) {
        g.unlocks.push(u.id);
        event(g, "unlock", u.level, `Unlocked ${u.label}`, now);
      }
  }
}

/** Bring the day and week up to date (call before anything else). */
export function rollover(g: Game, now: number, tz: string): void {
  const today = localDate(now, tz);
  if (g.today.date !== today) g.today = { date: today, done: 0, xp: 0, cleared: false };
  const ws = weekStart(today);
  if (g.week.start !== ws) g.week = { start: ws, challenges: freshChallenges() };
  // (A streak that missed a working day shows as 0 in view(); it isn't cleared here, so a finish
  // the sync only sees after midnight can still extend it.)
}

function progress(g: Game, id: Challenge["id"], by: number, now: number): void {
  const c = g.week.challenges.find((x) => x.id === id);
  if (!c || c.done) return;
  c.have = Math.min(c.goal, c.have + by);
  if (c.have >= c.goal) {
    c.done = true;
    event(g, "challenge", 150, c.label, now);
    gain(g, 150, now);
  }
}

/**
 * A ticket was finished. Returns the XP it earned (0 if it already paid out).
 * `planned`: it was one of today's quests. `allPlannedDone`: that finished the day's quests.
 */
export function finish(g: Game, t: Task, opts: { now: number; tz: string; planned: boolean; allPlannedDone: boolean }): number {
  const { now, tz } = opts;
  rollover(g, now, tz);
  if (g.awarded.includes(t.id)) return 0;
  g.awarded.push(t.id);
  if (g.awarded.length > 500) g.awarded.splice(0, g.awarded.length - 500);
  const today = g.today.date;
  // When it was really finished (a sync can see it later): that's the day and the combo it counts for.
  const at = Math.min(now, t.doneAt ?? now);
  const day = localDate(at, tz);
  const boss = wasBoss(t, day);

  // Combo: one after another within 45 minutes (by when they were finished).
  const last = g.combo.at ?? 0;
  const chained = g.combo.n > 0 && Math.abs(at - last) <= COMBO_MS;
  g.combo = { n: chained ? Math.min(g.combo.n + 1, 6) : 1, until: Math.max(at, last) + COMBO_MS, at: Math.max(at, last) };
  const comboBonus = 1 + 0.1 * Math.min(Math.max(g.combo.n - 1, 0), 5);

  let xp = PRIORITY_XP[t.priority] + Math.round(Math.min(t.estMin ?? 0, 480) / 15);
  if (opts.planned) xp *= 1.5;
  if (boss) xp *= 2;
  xp = Math.round(xp * comboBonus);

  event(g, boss ? "boss" : "xp", xp, boss ? `Defeated ${t.key}` : t.key, now);
  if (g.combo.n >= 2) event(g, "combo", g.combo.n, `Combo x${g.combo.n}`, now);
  gain(g, xp, now);
  if (day === today) g.today.done++;
  if (boss) g.bosses++;

  // Streak: by the day it was finished.
  if (!g.lastDay || day > g.lastDay) {
    g.streak = g.lastDay && g.lastDay >= previousWorkday(day) ? g.streak + 1 : 1;
    g.lastDay = day;
    g.best = Math.max(g.best, g.streak);
    if (g.streak >= 2) event(g, "streak", g.streak, `${g.streak} day streak`, now);
  }

  progress(g, "finish", 1, now);
  if (boss) progress(g, "boss", 1, now);
  if (opts.allPlannedDone && !g.today.cleared) {
    g.today.cleared = true;
    event(g, "cleared", 100, "Day cleared", now);
    gain(g, 100, now);
    progress(g, "clear", 1, now);
  }
  return xp;
}

/** A focus block ended: 10 XP per 25 minutes (partial blocks of 5+ minutes count pro rata). */
export function focused(g: Game, minutes: number, label: string, now: number, tz: string): number {
  rollover(g, now, tz);
  if (minutes < 5) return 0;
  g.focusMin += minutes;
  const xp = Math.max(2, Math.round((minutes / 25) * 10));
  event(g, "focus", xp, label, now);
  gain(g, xp, now);
  progress(g, "focus", minutes, now);
  return xp;
}

/** The compact score every screen gets. */
export function view(g: Game, now: number, tz = "UTC") {
  const level = levelFor(g.xp);
  const today = localDate(now, tz);
  const alive = !!g.lastDay && (g.lastDay === today || g.lastDay >= previousWorkday(today));
  return {
    xp: g.xp,
    level,
    levelXp: g.xp - levelStart(level),
    nextXp: levelStart(level + 1) - levelStart(level),
    streak: alive ? g.streak : 0,
    best: g.best,
    combo: g.combo.until >= now ? g.combo.n : 0,
    comboUntil: g.combo.until >= now ? g.combo.until : 0,
    todayXp: g.today.xp,
    todayDone: g.today.done,
    cleared: g.today.cleared,
    bosses: g.bosses,
    focusMin: g.focusMin,
    unlocks: g.unlocks,
    week: g.week.challenges,
    events: g.events.slice(-8),
    seq: g.seq,
  };
}

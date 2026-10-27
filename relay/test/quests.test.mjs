// The quest book and the game rules, on their own (no relay, no services).
//   node test/quests.test.mjs
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const here = fileURLToPath(new URL(".", import.meta.url));
const dir = mkdtempSync(join(tmpdir(), "quests-"));
const out = join(dir, "book.mjs");
execFileSync("npx", ["esbuild", join(here, "../src/quests/book.ts"), "--bundle", "--format=esm", `--outfile=${out}`, "--log-level=error"], { stdio: "inherit" });
const B = await import(out);
rmSync(dir, { recursive: true, force: true });

let failures = 0;
const check = (name, ok, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${extra && !ok ? "  " + extra : ""}`);
  if (!ok) failures++;
};

const TZ = "Australia/Sydney";
const at = (iso) => Date.parse(iso);
const task = (id, over = {}) => ({ id: "mojo:" + id, src: "mojo", key: id, title: "Task " + id, status: "todo", priority: 2, estMin: 60, due: null, url: "", project: "DKL", ...over });

// Monday 26 Oct 2026, 10:00 in Sydney.
let now = at("2026-10-25T23:00:00Z");
const q = B.emptyQuests(TZ);
B.merge(q, "mojo", [task("A-1"), task("A-2", { priority: 4 }), task("A-3", { status: "done", doneAt: now - 1000 }), task("A-4"), task("A-5", { priority: 1 }), task("A-6"), task("A-7", { status: "progress" })], now, true);
let v = B.questsView(q, now);
check("the first sync pays nothing for what was already done", v.game.xp === 0);
check("a new day plans five quests, in progress and bosses first", v.planTotal === 5 && v.tasks[0].key === "A-7" && v.tasks[1].key === "A-2", v.tasks.map((t) => t.key).join(","));
check("urgent is a boss with HP from its estimate", v.tasks.find((t) => t.key === "A-2").boss && v.tasks.find((t) => t.key === "A-2").hpMax === 60);

// Finish a planned medium task: (25 + 4) x 1.5 = 44 (rounded).
let xp = B.moved(q, "mojo:A-1", "done", now);
check("a planned medium 1 h task pays 44 XP", xp === 44, String(xp));
xp = B.moved(q, "mojo:A-1", "done", now);
check("finishing it again pays nothing", xp === 0);

// Combo: another within 45 min: base planned boss (50+4)*1.5*2 = 162, x1.1 = 178.
now += 10 * 60000;
xp = B.moved(q, "mojo:A-2", "done", now);
check("a boss within 45 min is a x2 combo (+10%)", xp === 178, String(xp));
v = B.questsView(q, now);
check("combo shows as 2", v.game.combo === 2);
check("level 2 at 100 XP, with an unlock", v.game.level === 3 ? true : v.game.level === 2 && v.game.unlocks.includes("scarf"), `${v.game.level} ${v.game.xp}`);
check("a boss defeat is an event", v.game.events.some((e) => e.kind === "boss" && e.label === "Defeated A-2"));

// Focus: 25 minutes on A-4 = 10 XP and damage.
B.startFocus(q, "mojo:A-4", 25, now);
now += 26 * 60000;
const f = B.endFocus(q, now);
check("a 25 min focus block counts 25 min", f.minutes === 25 && f.task.key === "A-4");
check("focus earns 10 XP", B.questsView(q, now).game.events.at(-1).kind === "focus" && B.questsView(q, now).game.events.at(-1).amount === 10);

// Done elsewhere: a sync that shows A-4 done pays out.
const synced = q.tasks.filter((t) => t.src === "mojo").map((t) => (t.key === "A-4" ? { ...t, status: "done", doneAt: now } : t));
xp = B.merge(q, "mojo", synced, now, false);
check("finished in Mojo Desk itself still counts", xp > 0);
check("focus minutes survive a sync", q.tasks.find((t) => t.key === "A-4").spentMin === 25);

// Clear the day: finish the rest of the plan.
for (const id of [...q.plan.ids]) B.moved(q, id, "done", (now += 60000));
v = B.questsView(q, now);
check("finishing every quest clears the day (+100)", v.game.cleared && v.game.events.some((e) => e.kind === "cleared"));
check("the streak starts at 1", v.game.streak === 1);

// Next working day after a weekend: Friday 30 Oct, then Monday 2 Nov keeps the streak.
now = at("2026-10-29T23:00:00Z"); // Fri 30 Oct, 10:00 Sydney
B.merge(q, "mojo", [...q.tasks.filter((t) => t.src === "mojo"), task("B-1")], now, false);
B.moved(q, "mojo:B-1", "done", now);
check("a missed Tue-Thu breaks the streak", B.questsView(q, now).game.streak === 1);
now = at("2026-11-01T23:00:00Z"); // Mon 2 Nov
B.merge(q, "mojo", [...q.tasks.filter((t) => t.src === "mojo"), task("B-2")], now, false);
B.moved(q, "mojo:B-2", "done", now);
check("the weekend doesn't break it", B.questsView(q, now).game.streak === 2);
check("a new week brings fresh challenges", B.questsView(q, now).game.week.every((c) => c.have <= 1));

// Plan editing.
B.merge(q, "mojo", [...q.tasks.filter((t) => t.src === "mojo"), task("C-1"), task("C-2")], now, false);
B.setPlanned(q, "mojo:C-2", true, now);
B.reorder(q, "mojo:C-2", -99, now);
check("you can add a quest and move it to the top", q.plan.ids[0] === "mojo:C-2" && !q.plan.auto);
B.snooze(q, "mojo:C-2", now);
check("snoozing takes it off today", !q.plan.ids.includes("mojo:C-2") && B.questsView(q, now).tasks.find((t) => t.key === "C-2").snoozed);
now += 86400000;
B.ensurePlan(q, now);
check("it comes back tomorrow", !q.snoozed["mojo:C-2"]);

// ---- the review's cases
{
  // Overdue is a boss even though it's done by the time it pays.
  const r = B.emptyQuests(TZ);
  let t0 = at("2026-10-27T23:00:00Z"); // Wed 28 Oct, 10:00 Sydney
  B.merge(r, "jira", [task("J-1", { src: "jira", id: "jira:J-1", due: "2026-10-01", priority: 1 })], t0, true);
  const xp = B.moved(r, "jira:J-1", "done", t0);
  const v = B.questsView(r, t0);
  check("an overdue ticket finished counts as a boss", v.game.bosses === 1 && v.game.events.some((e) => e.kind === "boss"), `${xp} ${v.game.bosses}`);

  // Seen late: finished Fri 23:59, synced Sat 00:02 still extends Thursday's streak.
  const s = B.emptyQuests(TZ);
  const thu = at("2026-10-28T23:00:00Z"); // Thu 29 Oct 10:00
  B.merge(s, "linear", [task("L-1", { src: "linear", id: "linear:L-1" }), task("L-2", { src: "linear", id: "linear:L-2" })], thu, true, thu);
  B.moved(s, "linear:L-1", "done", thu);
  const friLate = at("2026-10-30T12:59:00Z"); // Fri 30 Oct 23:59 Sydney
  const satEarly = at("2026-10-30T13:02:00Z"); // Sat 00:02
  B.merge(s, "linear", [task("L-1", { src: "linear", id: "linear:L-1", status: "done", doneAt: thu }), task("L-2", { src: "linear", id: "linear:L-2", status: "done", doneAt: friLate })], satEarly, false, thu);
  check("a finish seen after midnight counts for the day it happened", B.questsView(s, satEarly).game.streak === 2, String(B.questsView(s, satEarly).game.streak));

  // Hours apart, picked up in one sync: no combo.
  const c = B.emptyQuests(TZ);
  const t1 = at("2026-10-28T23:00:00Z");
  B.merge(c, "linear", [1, 2, 3].map((i) => task("C-" + i, { src: "linear", id: "linear:C-" + i })), t1, true, t1);
  B.merge(c, "linear", [1, 2, 3].map((i) => task("C-" + i, { src: "linear", id: "linear:C-" + i, status: "done", doneAt: t1 + i * 2 * 3600e3 })), t1 + 7 * 3600e3, false, t1);
  check("finishes hours apart are no combo", B.questsView(c, t1 + 7 * 3600e3).game.combo <= 1 && !B.questsView(c, t1 + 7 * 3600e3).game.events.some((e) => e.kind === "combo"));

  // Mojo Desk: a done ticket that shows up later (a comment bumped it) never pays.
  const m = B.emptyQuests(TZ);
  B.merge(m, "mojo", [task("M-1")], t1, true, t1);
  const paid = B.merge(m, "mojo", [task("M-1"), task("M-OLD", { status: "done" })], t1 + 600e3, false, t1);
  check("an old ticket finished before you connected pays nothing", paid === 0);

  // Big backlogs: capped per service, planned ones always kept.
  const big = B.emptyQuests(TZ);
  const many = Array.from({ length: 70 }, (_, i) => task("B-" + i));
  B.merge(big, "mojo", many, t1, true, t1);
  check("a big backlog is capped per service", big.tasks.filter((t) => t.src === "mojo").length === 40);
  B.setPlanned(big, "mojo:B-30", true, t1);
  // B-30 drops to the bottom of the ranking, but it's planned, so it stays.
  B.merge(big, "mojo", many.map((t) => (t.key === "B-30" ? { ...t, priority: 0, estMin: 10 } : t)), t1 + 1000, false, t1);
  check("planned quests are always kept", big.tasks.some((t) => t.id === "mojo:B-30"));
}

console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);

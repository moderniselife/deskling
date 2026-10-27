# Quests

Quests turn the tickets assigned to you in **Mojo Desk**, **Linear** and **Jira** into a game on
Deskling and in Deskling for your browser (`/app`). Your relay fetches them, keeps the score and
writes changes back, so the board and every browser see the same game.

## Connect a service

In `/app`, open **Quests**, then **Connect**, pick the service and paste a key. The relay checks
the key straight away (it shows whose it is) and stores it. Keys are never sent back out, not even
to `/app` or `/api/state`. **Disconnect** removes the key and that service's quests.

| Service | What to paste | Where to get it |
| --- | --- | --- |
| Mojo Desk | your Mojo Desk address and an API token | Mojo Desk, Settings, API tokens |
| Linear | a personal API key | Linear, Settings, Security & access, Personal API keys |
| Jira | your site (`you.atlassian.net`), your Atlassian email and an API token | id.atlassian.com, Security, API tokens |

The relay syncs every 3 minutes, after anything you do, and when you tap **Sync**. It reads your
open tickets plus the ones finished in the last week, so finishing a ticket in the service itself
still counts. The first sync of a service only learns what's there: what was already done pays
nothing.

## Playing

- **Today's quests.** Each day starts with five quests picked for you (in progress first, then
  bosses, then by priority and due date). Add more from **More quests**, move them up or down,
  take one **Off the list**, or say **Not today** (it comes back tomorrow). While you haven't
  changed the plan, it fills up as other services' tickets arrive.
- **Finishing.** Tap a quest's check, then tap again within 3 seconds (on the board), or open it
  and tap **Done**. **Start** and **Ready to test** move it in its service too. Jira moves through
  the matching transition, and Linear moves to the team's matching state.
- **Bosses.** Urgent tickets, tickets estimated at 4 hours or more, and overdue ones are bosses.
  Their HP is the estimate in minutes (an hour at least), and every focus minute on one is a point
  of damage.
- **Focus.** **Focus 25 min** fills the board with a countdown. Ending it (or stopping after 5
  minutes or more) earns XP, and on Jira the time is logged as a worklog.
- **Hand to Claude.** Opens New session with a prompt that names the ticket and links it.

## Score

| | |
| --- | --- |
| Finishing a ticket | urgent 50, high 35, medium 25, low or none 15, plus 1 per 15 estimated minutes (up to 8 h) |
| One of today's quests | ×1.5 |
| A boss | ×2 |
| Combo | another ticket within 45 minutes of the last: +10% per ticket in a row, up to +50% |
| Day cleared | every quest of the day done: +100 |
| Focus | 10 per 25 minutes |
| Weekly challenges (from Monday) | finish 10 quests, defeat 2 bosses, focus for 5 hours, clear 3 days: +150 each |
| Streak | working days in a row with something finished (weekends don't break it) |
| Levels | level n starts at 50·n·(n−1) XP: 100, 300, 600, 1000… |
| Unlocks | level 2 a scarf, 3 glasses, 5 focus headphones, 7 a party hat, 10 the crown (Deskling wears them) |

Each event plays once on every screen: a floating "+44 XP", a shaking combo, a boss-defeat burst,
a level-up card with confetti and the new outfit, "Day cleared", challenge cards and streaks.
Screens that open later don't replay old news. Level-ups also go to the feed, and so to
notifications.

## How it's built

- `relay/src/quests/`: `providers.ts` (Mojo Desk REST, Linear GraphQL, Jira REST v3 with the
  `/search/jql` endpoint), `book.ts` (merging, the day's plan, focus, the view every screen gets),
  `game.ts` (the rules above, pure functions).
- The hub stores keys under their own storage key (`integrations`), syncs on the Durable Object
  alarm alongside reminders, and takes `quest_status`, `quest_plan`, `quest_move`, `quest_snooze`,
  `quest_focus` and `quest_sync` over the WebSocket. Snapshots carry a compact `quests` block.
- Board: `ui/scr_quests.c` (the screen and the focus timer), `ui/celebrate.c` (the events),
  `deskling_set_outfit()` (the unlocks), and the Quests chip on Home's Deskling card.
- Tests: `relay/test/quests.test.mjs` (rules), `relay/test/integrations.test.mjs` (the relay against
  fake Mojo Desk, Linear and Jira servers), the browser test, and the simulator (screens, actions,
  focus, celebrations, and drawing benchmarks for each).

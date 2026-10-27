// Deskling's view of the world: a parsed copy of the relay's latest snapshot,
// plus helpers that build the messages the screen sends back.
// Pure C + cJSON, so the same code runs on the ESP32-P4 and in the host simulator.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DK_MAX_AGENTS 12
#define DK_MAX_FEED 12
#define DK_MAX_REMINDERS 12
#define DK_MAX_PENDING 4
#define DK_MAX_CHAT 30
#define DK_MAX_HELPERS 4
#define DK_MAX_QUESTIONS 4
#define DK_MAX_OPTIONS 6

typedef enum { DK_IDLE, DK_WORKING, DK_WAITING, DK_NEEDS_YOU, DK_DONE } dk_state_t;
typedef enum { DK_SRC_CLAUDE, DK_SRC_CURSOR, DK_SRC_CODEX, DK_SRC_GEMINI, DK_SRC_OPENCODE, DK_SRC_CUSTOM } dk_source_t;
typedef enum { DK_FEED_INFO, DK_FEED_GOOD, DK_FEED_ALERT, DK_FEED_REMINDER } dk_level_t;
typedef enum { DK_CHAT_QUEUED, DK_CHAT_DELIVERED, DK_CHAT_SENT, DK_CHAT_TYPED } dk_chat_status_t;

typedef enum {
    DK_CAUSE_HELLO,
    DK_CAUSE_AGENT,
    DK_CAUSE_FEED,
    DK_CAUSE_PERMISSION,
    DK_CAUSE_QUESTION,
    DK_CAUSE_PERMISSION_CLOSED,
    DK_CAUSE_DONE,
    DK_CAUSE_PING,
    DK_CAUSE_CHAT,
    DK_CAUSE_REMINDER,
    DK_CAUSE_REMINDER_DUE,
    DK_CAUSE_REMINDER_DONE,
    DK_CAUSE_FOCUS,
    DK_CAUSE_QUESTS,
} dk_cause_t;

typedef struct {
    char id[16];
    char text[1216];
    bool from_you;
    bool step;    // a tool step it took ("Editing scr_chat.c"), not a message
    bool thought; // a line of its thinking (shown like a step)
    char image[24]; // a picture in this message, fetched from the relay by id
    char quote[420]; // the bit of Claude's message you replied to
    bool more;       // the reply goes on: dk_send_detail fetches all of it
    int hidden;      // "6 earlier steps": how many steps this line stands for (dk_send_detail lists them)
    dk_chat_status_t status;
    int64_t ts;
} dk_chat_t;

/* A helper (subagent) the session handed part of its work to. */
typedef struct {
    char id[40];
    char name[64];
    char task[96];
    bool done;
} dk_helper_t;

typedef struct {
    char id[72];
    dk_source_t source;
    char name[48];
    char host[48];
    char os[16];
    dk_state_t state;
    char task[96];
    char last_prompt[168];
    char last_message[288];
    int64_t since;
    int64_t updated;
    bool channel;
    bool can_message;
    dk_chat_t chat[DK_MAX_CHAT];
    int chat_n;
    dk_helper_t helpers[DK_MAX_HELPERS];
    int helpers_n;
    bool listening;       // "Wait for my reply" is on: when it finishes it waits for a message from here
    bool holding;         // ...and it has finished: Claude is waiting for your reply right now
    int64_t listen_until; // relay clock, ms
    char model[40];       // model of its latest reply, e.g. "claude-opus-5-5"
    int32_t context;      // tokens in its context window
} dk_agent_t;

typedef struct {
    char id[16];
    dk_level_t level;
    char title[64];
    char body[148];
    char agent_id[72];
    int64_t ts;
} dk_feed_t;

typedef struct {
    char id[16];
    char title[88];
    int64_t due; // ms epoch, 0 = no time
    char from[32];
    bool done;
} dk_reminder_t;

typedef struct {
    char label[64];
    char description[124];
} dk_option_t;

typedef struct {
    char question[204];
    char header[28];
    dk_option_t options[DK_MAX_OPTIONS];
    int option_n;
    bool multi;
} dk_question_t;

typedef struct {
    char id[16];
    bool is_question;
    char agent_id[72];
    dk_source_t source;
    char name[48];
    char host[48];
    char tool[48];
    char summary[164];
    char detail[124];
    dk_question_t questions[DK_MAX_QUESTIONS];
    int question_n;
    int64_t expires;
} dk_pending_t;

#define DK_MAX_TARGETS 6
#define DK_MAX_PROJECTS 12

#define DK_MAX_ENGINES 8
#define DK_MAX_MODELS 8

/* A model OpenCode can use on that computer ("ollama/qwen3:8b", "Qwen3 8B"). */
typedef struct {
    char id[64];
    char name[32];
} dk_model_choice_t;

/* An agent a computer can start: "claude" (Claude Code), "opencode" with its models, or another
   agent the runner speaks ACP with ("gemini", "cursor", "goose"…). */
typedef struct {
    char id[24]; // the relay allows up to 16 characters
    char name[24];
    dk_model_choice_t models[DK_MAX_MODELS];
    int models_n;
} dk_engine_t;

/* Somewhere the screen can start a new session: the cloud, or a computer running the runner. */
typedef struct {
    char id[48];
    char name[48];
    bool cloud;
    char projects[DK_MAX_PROJECTS][48];
    int projects_n;
    dk_engine_t engines[DK_MAX_ENGINES]; // none: Claude Code only (the cloud, older runners)
    int engines_n;
} dk_target_t;

/* ---- Quests: your tickets from Mojo Desk, Linear and Jira, and the game around them. */
#define DK_MAX_QUESTS 30
#define DK_MAX_GAME_EVENTS 8

typedef enum { DK_QSRC_MOJO, DK_QSRC_LINEAR, DK_QSRC_JIRA } dk_qsrc_t;
typedef enum { DK_Q_TODO, DK_Q_PROGRESS, DK_Q_REVIEW, DK_Q_DONE } dk_qstatus_t;
typedef enum { DK_EV_XP, DK_EV_LEVEL, DK_EV_COMBO, DK_EV_BOSS, DK_EV_CLEARED, DK_EV_CHALLENGE, DK_EV_UNLOCK, DK_EV_STREAK, DK_EV_FOCUS } dk_event_kind_t;

/* Unlocked Deskling outfits (bits). */
#define DK_OUTFIT_SCARF (1u << 0)
#define DK_OUTFIT_GLASSES (1u << 1)
#define DK_OUTFIT_HEADPHONES (1u << 2)
#define DK_OUTFIT_PARTY_HAT (1u << 3)
#define DK_OUTFIT_CROWN (1u << 4)

typedef struct {
    char id[56];
    dk_qsrc_t src;
    char key[24];
    char title[124];
    dk_qstatus_t status;
    uint8_t prio; // 0 none … 4 urgent
    uint16_t est; // minutes
    char due[12];
    bool overdue, planned, snoozed, boss;
    uint16_t hp, hp_max;
    char url[160];
} dk_quest_t;

typedef struct {
    dk_event_kind_t kind;
    int32_t amount;
    char label[48];
    uint32_t seq;
} dk_game_event_t;

typedef struct {
    char label[32];
    int32_t goal, have;
    bool done;
} dk_challenge_t;

typedef struct {
    bool on;            // there's something to show (a service connected, or a score)
    uint8_t connected;  // bits: 1 << dk_qsrc_t
    uint8_t failing;    // ...whose last sync failed
    char error[96];     // the first failure's reason
    dk_quest_t tasks[DK_MAX_QUESTS]; // today's quests first (in plan order), then the rest
    int task_n;
    int plan_done, plan_total;
    char focus_id[56]; // a focus block on this quest ("" = none)
    int64_t focus_started, focus_ends;
    int focus_minutes;
    int32_t xp, level, level_xp, next_xp, streak, best, combo, today_xp, today_done, bosses, focus_min;
    int64_t combo_until;
    bool cleared;
    uint32_t outfits; // DK_OUTFIT_* unlocked
    dk_challenge_t week[4];
    int week_n;
    dk_game_event_t events[DK_MAX_GAME_EVENTS];
    int event_n;
    uint32_t seq;
} dk_quests_t;

typedef struct {
    int64_t now; // relay clock at snapshot time (ms)
    dk_cause_t cause;
    char cause_id[72];
    dk_agent_t agents[DK_MAX_AGENTS];
    int agent_n;
    dk_feed_t feed[DK_MAX_FEED];
    int feed_n;
    dk_reminder_t reminders[DK_MAX_REMINDERS];
    int reminder_n;
    dk_pending_t pending[DK_MAX_PENDING];
    int pending_n;
    bool focus;
    bool ping_when_done;
    dk_target_t targets[DK_MAX_TARGETS];
    int targets_n;
    dk_quests_t quests;
} dk_model_t;

/** Parse a relay snapshot. Returns false if it isn't a snapshot (out is untouched then). */
bool dk_model_parse(const char *json, size_t len, dk_model_t *out);

const dk_agent_t *dk_model_find_agent(const dk_model_t *m, const char *id);

/** Where outgoing messages go: the WebSocket on the device, stdout in the simulator. */
typedef void (*dk_send_fn)(const char *json);
void dk_set_sender(dk_send_fn fn);

void dk_send_decision(const char *pending_id, const char *decision); // "allow" | "deny" | "computer"
/** labels[i] is the chosen label for question i; multi-select answers are joined with "\n". */
void dk_send_answer(const dk_pending_t *p, const char *const *labels);
void dk_send_message(const char *agent_id, const char *text);
/* "Wait for my reply" for minutes (0 = stop): when the session finishes, it waits for a message from the screen. */
void dk_send_listen(const char *agent_id, int minutes);
/* Start a new session on a target ("cloud" or a computer), in one of its projects, with a first
   prompt; engine and model are optional ("" = Claude Code). */
void dk_send_start(const char *target, const char *project, const char *prompt, const char *engine, const char *model);
void dk_send_reminder_done(const char *reminder_id, bool done);
void dk_send_action(const char *name); // "focus" | "snooze_all" | "ping_when_done"
/** Remove one notification from the feed, or all of them when feed_id is NULL. */
void dk_send_feed_dismiss(const char *feed_id);
void dk_send_dismiss(const char *agent_id);
void dk_send_hello(void);
/* Which chat is open ("" for none): the relay sends that one in full and only the latest lines of
   the others, which keeps snapshots small. dk_resend_view repeats it after a reconnect. */
void dk_send_view(const char *agent_id);
void dk_resend_view(void);
/* Ask for the whole of a long reply, or the steps behind an "N earlier steps" line. The answer
   arrives as a "detail" message (dk_detail_parse). */
void dk_send_detail(const char *agent_id, const char *line_id);

/* Quests. */
void dk_send_quest_status(const char *id, dk_qstatus_t to);
void dk_send_quest_plan(const char *id, bool on);
void dk_send_quest_move(const char *id, int by);
void dk_send_quest_snooze(const char *id);
void dk_send_quest_focus(const char *id, int minutes); // minutes 0 = stop the current block
void dk_send_quest_sync(void);
const char *dk_qsrc_label(dk_qsrc_t s);
const dk_quest_t *dk_quest_find(const dk_model_t *m, const char *id);

typedef struct {
    bool thought;
    char *text;
} dk_detail_step_t;

/* The answer to dk_send_detail. text and steps are heap copies: free with dk_detail_free. */
typedef struct {
    char agent_id[72];
    char line_id[24];
    char *text;              // a long reply in full (NULL for steps)
    dk_detail_step_t *steps; // the hidden steps, oldest first
    int steps_n;
} dk_detail_t;

/** Parse a "detail" message. Returns false if it isn't one. */
bool dk_detail_parse(const char *json, size_t len, dk_detail_t *out);
void dk_detail_free(dk_detail_t *d);

const char *dk_state_label(dk_state_t s);
const char *dk_source_label(dk_source_t s);

#ifdef __cplusplus
}
#endif

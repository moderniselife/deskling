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
} dk_cause_t;

typedef struct {
    char id[16];
    char text[1216];
    bool from_you;
    bool step;    // a tool step it took ("Editing scr_chat.c"), not a message
    bool thought; // a line of its thinking (shown like a step)
    char image[24]; // a picture in this message, fetched from the relay by id
    char quote[420]; // the bit of Claude's message you replied to
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
    bool listening;       // "Keep listening" is on: when it finishes it waits for a message from here
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
    char id[16];
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
/* Keep listening for minutes (0 = stop): when the session finishes, it waits for a message from the screen. */
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

const char *dk_state_label(dk_state_t s);
const char *dk_source_label(dk_source_t s);

#ifdef __cplusplus
}
#endif

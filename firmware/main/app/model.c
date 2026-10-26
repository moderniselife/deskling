#include "model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static dk_send_fn s_send;

static void copy_str(char *dst, size_t cap, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        // Copy without splitting a UTF-8 sequence at the end.
        size_t n = strlen(v->valuestring);
        if (n >= cap) {
            n = cap - 1;
            while (n > 0 && ((unsigned char)v->valuestring[n] & 0xC0) == 0x80) {
                n--;
            }
        }
        memcpy(dst, v->valuestring, n);
        dst[n] = '\0';
    } else {
        dst[0] = '\0';
    }
}

static int64_t get_i64(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : 0;
}

static bool get_bool(const cJSON *obj, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

static bool str_is(const cJSON *obj, const char *key, const char *want)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring && strcmp(v->valuestring, want) == 0;
}

static dk_state_t parse_state(const cJSON *o)
{
    if (str_is(o, "state", "working")) return DK_WORKING;
    if (str_is(o, "state", "waiting")) return DK_WAITING;
    if (str_is(o, "state", "needs_you")) return DK_NEEDS_YOU;
    if (str_is(o, "state", "done")) return DK_DONE;
    return DK_IDLE;
}

static dk_source_t parse_source(const cJSON *o)
{
    if (str_is(o, "source", "cursor")) return DK_SRC_CURSOR;
    if (str_is(o, "source", "codex")) return DK_SRC_CODEX;
    if (str_is(o, "source", "gemini")) return DK_SRC_GEMINI;
    if (str_is(o, "source", "custom")) return DK_SRC_CUSTOM;
    return DK_SRC_CLAUDE;
}

static dk_cause_t parse_cause(const cJSON *c)
{
    static const struct {
        const char *name;
        dk_cause_t cause;
    } map[] = {
        {"agent", DK_CAUSE_AGENT}, {"feed", DK_CAUSE_FEED}, {"permission", DK_CAUSE_PERMISSION},
        {"question", DK_CAUSE_QUESTION}, {"permission_closed", DK_CAUSE_PERMISSION_CLOSED}, {"done", DK_CAUSE_DONE},
        {"ping", DK_CAUSE_PING}, {"chat", DK_CAUSE_CHAT}, {"reminder", DK_CAUSE_REMINDER},
        {"reminder_due", DK_CAUSE_REMINDER_DUE}, {"reminder_done", DK_CAUSE_REMINDER_DONE}, {"focus", DK_CAUSE_FOCUS},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (str_is(c, "kind", map[i].name)) return map[i].cause;
    }
    return DK_CAUSE_HELLO;
}

bool dk_model_parse(const char *json, size_t len, dk_model_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    if (!str_is(root, "type", "snapshot")) {
        cJSON_Delete(root);
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->now = get_i64(root, "now");
    const cJSON *cause = cJSON_GetObjectItemCaseSensitive(root, "cause");
    out->cause = parse_cause(cause);
    copy_str(out->cause_id, sizeof(out->cause_id), cause, "id");
    out->focus = get_bool(root, "focus");
    out->ping_when_done = get_bool(root, "pingWhenDone");

    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "agents"))
    {
        if (out->agent_n >= DK_MAX_AGENTS) break;
        dk_agent_t *a = &out->agents[out->agent_n++];
        copy_str(a->id, sizeof(a->id), it, "id");
        a->source = parse_source(it);
        copy_str(a->name, sizeof(a->name), it, "name");
        copy_str(a->host, sizeof(a->host), it, "host");
        copy_str(a->os, sizeof(a->os), it, "os");
        a->state = parse_state(it);
        copy_str(a->task, sizeof(a->task), it, "task");
        copy_str(a->last_prompt, sizeof(a->last_prompt), it, "lastPrompt");
        copy_str(a->last_message, sizeof(a->last_message), it, "lastMessage");
        a->since = get_i64(it, "since");
        a->updated = get_i64(it, "updated");
        a->channel = get_bool(it, "channel");
        a->can_message = get_bool(it, "canMessage");
        a->listening = get_bool(it, "listening");
        a->listen_until = get_i64(it, "listenUntil");
        copy_str(a->model, sizeof(a->model), it, "model");
        a->context = (int32_t)get_i64(it, "context");
        const cJSON *c;
        const cJSON *chat = cJSON_GetObjectItemCaseSensitive(it, "chat");
        int skip = cJSON_GetArraySize(chat) - DK_MAX_CHAT; // keep the newest lines
        cJSON_ArrayForEach(c, chat)
        {
            if (skip-- > 0) continue;
            if (a->chat_n >= DK_MAX_CHAT) break;
            dk_chat_t *line = &a->chat[a->chat_n++];
            copy_str(line->id, sizeof(line->id), c, "id");
            copy_str(line->text, sizeof(line->text), c, "text");
            line->from_you = str_is(c, "from", "you");
            line->thought = str_is(c, "from", "thought");
            line->step = line->thought || str_is(c, "from", "tool");
            copy_str(line->image, sizeof(line->image), c, "image");
            copy_str(line->quote, sizeof(line->quote), c, "quote");
            line->status = str_is(c, "status", "queued") ? DK_CHAT_QUEUED : str_is(c, "status", "delivered") ? DK_CHAT_DELIVERED : str_is(c, "status", "typed") ? DK_CHAT_TYPED : DK_CHAT_SENT;
            line->ts = get_i64(c, "ts");
        }
        cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(it, "helpers"))
        {
            if (a->helpers_n >= DK_MAX_HELPERS) break;
            dk_helper_t *h = &a->helpers[a->helpers_n++];
            copy_str(h->id, sizeof(h->id), c, "id");
            copy_str(h->name, sizeof(h->name), c, "name");
            copy_str(h->task, sizeof(h->task), c, "task");
            h->done = get_bool(c, "done");
        }
    }

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "targets"))
    {
        if (out->targets_n >= DK_MAX_TARGETS) break;
        dk_target_t *t = &out->targets[out->targets_n++];
        copy_str(t->id, sizeof(t->id), it, "id");
        copy_str(t->name, sizeof(t->name), it, "name");
        t->cloud = str_is(it, "kind", "cloud");
        const cJSON *pj;
        cJSON_ArrayForEach(pj, cJSON_GetObjectItemCaseSensitive(it, "projects"))
        {
            if (t->projects_n >= DK_MAX_PROJECTS || !cJSON_IsString(pj)) break;
            snprintf(t->projects[t->projects_n++], sizeof(t->projects[0]), "%s", pj->valuestring);
        }
    }

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "feed"))
    {
        if (out->feed_n >= DK_MAX_FEED) break;
        dk_feed_t *f = &out->feed[out->feed_n++];
        copy_str(f->id, sizeof(f->id), it, "id");
        f->level = str_is(it, "level", "good") ? DK_FEED_GOOD : str_is(it, "level", "alert") ? DK_FEED_ALERT : str_is(it, "level", "reminder") ? DK_FEED_REMINDER : DK_FEED_INFO;
        copy_str(f->title, sizeof(f->title), it, "title");
        copy_str(f->body, sizeof(f->body), it, "body");
        copy_str(f->agent_id, sizeof(f->agent_id), it, "agentId");
        f->ts = get_i64(it, "ts");
    }

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "reminders"))
    {
        if (out->reminder_n >= DK_MAX_REMINDERS) break;
        dk_reminder_t *r = &out->reminders[out->reminder_n++];
        copy_str(r->id, sizeof(r->id), it, "id");
        copy_str(r->title, sizeof(r->title), it, "title");
        r->due = get_i64(it, "due");
        copy_str(r->from, sizeof(r->from), it, "from");
        r->done = get_bool(it, "done");
    }

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "pending"))
    {
        if (out->pending_n >= DK_MAX_PENDING) break;
        dk_pending_t *p = &out->pending[out->pending_n++];
        copy_str(p->id, sizeof(p->id), it, "id");
        p->is_question = str_is(it, "kind", "question");
        copy_str(p->agent_id, sizeof(p->agent_id), it, "agentId");
        p->source = parse_source(it);
        copy_str(p->name, sizeof(p->name), it, "name");
        copy_str(p->host, sizeof(p->host), it, "host");
        copy_str(p->tool, sizeof(p->tool), it, "tool");
        copy_str(p->summary, sizeof(p->summary), it, "summary");
        copy_str(p->detail, sizeof(p->detail), it, "detail");
        p->expires = get_i64(it, "expires");
        const cJSON *q;
        cJSON_ArrayForEach(q, cJSON_GetObjectItemCaseSensitive(it, "questions"))
        {
            if (p->question_n >= DK_MAX_QUESTIONS) break;
            dk_question_t *dq = &p->questions[p->question_n++];
            copy_str(dq->question, sizeof(dq->question), q, "question");
            copy_str(dq->header, sizeof(dq->header), q, "header");
            dq->multi = get_bool(q, "multiSelect");
            const cJSON *o;
            cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(q, "options"))
            {
                if (dq->option_n >= DK_MAX_OPTIONS) break;
                dk_option_t *op = &dq->options[dq->option_n++];
                copy_str(op->label, sizeof(op->label), o, "label");
                copy_str(op->description, sizeof(op->description), o, "description");
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

const dk_agent_t *dk_model_find_agent(const dk_model_t *m, const char *id)
{
    if (!m || !id || !id[0]) return NULL;
    for (int i = 0; i < m->agent_n; i++) {
        if (strcmp(m->agents[i].id, id) == 0) return &m->agents[i];
    }
    return NULL;
}

// ------------------------------------------------------------- outgoing

void dk_set_sender(dk_send_fn fn)
{
    s_send = fn;
}

static void send_obj(cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    if (s) {
        if (s_send) s_send(s);
        cJSON_free(s);
    }
    cJSON_Delete(obj);
}

void dk_send_decision(const char *pending_id, const char *decision)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "decision");
    cJSON_AddStringToObject(o, "id", pending_id);
    cJSON_AddStringToObject(o, "decision", decision);
    send_obj(o);
}

void dk_send_answer(const dk_pending_t *p, const char *const *labels)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "answer");
    cJSON_AddStringToObject(o, "id", p->id);
    cJSON *answers = cJSON_AddObjectToObject(o, "answers");
    for (int i = 0; i < p->question_n; i++) {
        const char *l = labels[i] ? labels[i] : "";
        if (p->questions[i].multi) {
            cJSON *arr = cJSON_CreateArray();
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", l);
            for (char *tok = strtok(buf, "\n"); tok; tok = strtok(NULL, "\n")) {
                cJSON_AddItemToArray(arr, cJSON_CreateString(tok));
            }
            cJSON_AddItemToObject(answers, p->questions[i].question, arr);
        } else {
            cJSON_AddStringToObject(answers, p->questions[i].question, l);
        }
    }
    send_obj(o);
}

void dk_send_message(const char *agent_id, const char *text)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "message");
    cJSON_AddStringToObject(o, "agentId", agent_id);
    cJSON_AddStringToObject(o, "text", text);
    send_obj(o);
}

void dk_send_listen(const char *agent_id, int minutes)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "listen");
    cJSON_AddStringToObject(o, "agentId", agent_id);
    cJSON_AddNumberToObject(o, "minutes", minutes);
    send_obj(o);
}

void dk_send_start(const char *target, const char *project, const char *prompt)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "start");
    cJSON_AddStringToObject(o, "target", target);
    cJSON_AddStringToObject(o, "project", project ? project : "");
    cJSON_AddStringToObject(o, "prompt", prompt);
    send_obj(o);
}

void dk_send_reminder_done(const char *reminder_id, bool done)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "reminder_done");
    cJSON_AddStringToObject(o, "id", reminder_id);
    cJSON_AddBoolToObject(o, "done", done);
    send_obj(o);
}

void dk_send_action(const char *name)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "action");
    cJSON_AddStringToObject(o, "name", name);
    send_obj(o);
}

void dk_send_dismiss(const char *agent_id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "dismiss");
    cJSON_AddStringToObject(o, "agentId", agent_id);
    send_obj(o);
}

void dk_send_feed_dismiss(const char *feed_id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", feed_id ? "feed_dismiss" : "feed_clear");
    if (feed_id) cJSON_AddStringToObject(o, "id", feed_id);
    send_obj(o);
}

void dk_send_hello(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "hello");
    send_obj(o);
}

const char *dk_state_label(dk_state_t s)
{
    switch (s) {
    case DK_WORKING: return "Working";
    case DK_WAITING: return "Your turn";
    case DK_NEEDS_YOU: return "Needs you";
    case DK_DONE: return "Done";
    default: return "Idle";
    }
}

const char *dk_source_label(dk_source_t s)
{
    switch (s) {
    case DK_SRC_CURSOR: return "Cursor";
    case DK_SRC_CODEX: return "Codex";
    case DK_SRC_GEMINI: return "Gemini";
    case DK_SRC_CUSTOM: return "Agent";
    default: return "Claude Code";
    }
}

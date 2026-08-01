/**
 * app_tasks — the five task entry points and their docs/01 §FreeRTOS task
 * layout parameters. The table there is binding: names, cores and
 * priorities are transcribed here and nowhere else.
 *
 * T15 creates all five with stub bodies; T16-T18 fill them in.
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

/* Task            core prio   (docs/01 table) */
#define RADIO_TASK_CORE 1
#define RADIO_TASK_PRIO 12
#define RADIO_TASK_STACK 4096

#define VOICE_TASK_CORE 1
#define VOICE_TASK_PRIO 8
#define VOICE_TASK_STACK 4096

#define GPS_TASK_CORE 0
#define GPS_TASK_PRIO 6
#define GPS_TASK_STACK 4096

#define CTRL_TASK_CORE 0
#define CTRL_TASK_PRIO 5
#define CTRL_TASK_STACK 3072

#define UI_TASK_CORE 0
#define UI_TASK_PRIO 4
#define UI_TASK_STACK 4096

/** Heartbeat period for the stub bodies (logged at DEBUG). */
#define TASK_HEARTBEAT_MS 5000

void radio_task(void *arg);
void voice_task(void *arg);
void gps_task(void *arg);
void ui_task(void *arg);
void ctrl_task(void *arg);

#endif /* APP_TASKS_H */

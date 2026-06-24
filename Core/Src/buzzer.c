#include "buzzer.h"
#include "tim.h"

/* ── 엔벨로프 파라미터 ── */
#define ENVELOPE_STEP_MS   2    // CCR 감쇠 갱신 주기 (ms)
#define MAX_NOTES          4    // 시퀀스 당 최대 노트 수

/* ── 비상음 반복 간격 ── */
#define EMERGENCY_GAP_MS   120  // 각 띵 사이 묵음 구간 (ms)

/* ── 구조체 정의 ── */
typedef struct {
    uint32_t arr;         // TIM2 ARR (주파수 결정)
    uint32_t duration_ms; // 유지 시간
    uint32_t decay_rate;  // 감쇠 단계 크기
} Note_t;

typedef struct {
    Note_t  notes[MAX_NOTES];
    uint8_t count;        // 실제 사용 노트 수
    uint8_t loop;         // 반복 횟수
} Sequence_t;

/* ── 소리 시퀀스 정의 ── */
static const Sequence_t SEQ_DING = {
    .notes = {
        { NOTE_E5, 200, 1 }, // E5, 200ms, 느린 감쇠
        { NOTE_D5, 180, 2 }, // D5, 180ms, 조금 빠른 감쇠
    },
    .count = 2,
    .loop  = 1
};

static const Sequence_t SEQ_DONG = {
    .notes = {
        { NOTE_D5, 120, 3 }, // D5, 120ms, 빠른 감쇠
        { NOTE_B4, 200, 2 }, // B4, 200ms, 중간 감쇠
    },
    .count = 2,
    .loop  = 1
};

static const Sequence_t SEQ_EMERGENCY = {
    .notes = {
        { NOTE_E5, 200, 1 },
        { NOTE_D5, 180, 2 },
    },
    .count = 2,
    .loop  = 3   // 3회 반복
};

/* ── 플레이어 상태 ── */
static const Sequence_t *s_seq       = NULL;
static uint8_t           s_note_idx  = 0;   // 현재 시퀀스 내 노트 인덱스
static uint8_t           s_loop_rem  = 0;   // 남은 반복 횟수
static uint32_t          s_note_start= 0;   // 현재 노트 시작 tick
static uint32_t          s_env_tick  = 0;   // 마지막 엔벨로프 갱신 tick
static uint32_t          s_ccr_cur   = 0;   // 현재 CCR1 값
static uint8_t           s_active    = 0;   // 재생 중 플래그

/* ── 비상 반복 대기 상태 ── */
static uint8_t           s_gap_active = 0;  // 묵음 구간 여부
static uint32_t          s_gap_start  = 0;

/* ── 내부 함수 ── */
static void play_note(const Note_t *n)
{
    __HAL_TIM_SET_AUTORELOAD(&htim2, n->arr);
    s_ccr_cur = n->arr / 2;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, s_ccr_cur);
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    s_note_start = HAL_GetTick();
    s_env_tick   = s_note_start;
}

static void start_sequence(const Sequence_t *seq)
{
    s_seq        = seq;
    s_note_idx   = 0;
    s_loop_rem   = seq->loop;
    s_active     = 1;
    s_gap_active = 0;

    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    play_note(&seq->notes[0]);
}

static void stop_buzzer(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    s_active     = 0;
    s_gap_active = 0;
    s_seq        = NULL;
}

/* ── 공개 API ── */
void Buzzer_Ding(void)      { start_sequence(&SEQ_DING);      }
void Buzzer_Dong(void)      { start_sequence(&SEQ_DONG);      }
void Buzzer_Emergency(void) { start_sequence(&SEQ_EMERGENCY); }

/* ── 메인루프용 논블로킹 업데이트 함수 ── */
void Buzzer_Update(void)
{
    if (!s_active && !s_gap_active) return;

    uint32_t now = HAL_GetTick();

    /* 비상음 반복 사이 묵음 대기 처리 */
    if (s_gap_active)
    {
        if (now - s_gap_start >= EMERGENCY_GAP_MS)
        {
            s_gap_active = 0;
            s_note_idx = 0;
            s_active   = 1;
            HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
            play_note(&s_seq->notes[0]);
        }
        return;
    }

    const Note_t *cur = &s_seq->notes[s_note_idx];

    /* 엔벨로프: 설정 주기에 따라 CCR 감쇠 */
    if (now - s_env_tick >= ENVELOPE_STEP_MS)
    {
        s_env_tick = now;
        if (s_ccr_cur > cur->decay_rate)
        {
            s_ccr_cur -= cur->decay_rate;
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, s_ccr_cur);
        }
        else
        {
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
        }
    }

    /* 노트 시간 완료 시 다음 노트 이동 또는 종료 */
    if (now - s_note_start >= cur->duration_ms)
    {
        s_note_idx++;

        if (s_note_idx < s_seq->count)
        {
            play_note(&s_seq->notes[s_note_idx]);
        }
        else
        {
            s_loop_rem--;

            if (s_loop_rem > 0)
            {
                HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
                s_active     = 0;
                s_gap_active = 1;
                s_gap_start  = now;
            }
            else
            {
                stop_buzzer();
            }
        }
    }
}

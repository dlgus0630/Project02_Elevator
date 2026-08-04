#include "dev_buzzer.h"
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

/* ── 소리 시퀀스 정의 ──
 * 각 노트는 { 음(ARR), 유지 시간, 감쇠 속도 }. 감쇠 값이 클수록 소리가 빨리
 * 잦아들어 짧고 단단하게 들린다. */

/* 도착·문 열림: 높은음 → 낮은음으로 내려가며 부드럽게 여운을 남긴다 */
static const Sequence_t SEQ_DING = {
    .notes = {
        { NOTE_E5, 200, 1 },
        { NOTE_D5, 180, 2 },
    },
    .count = 2,
    .loop  = 1
};

/* 문 닫힘: 앞음을 짧고 빠르게 끊어 "동~" 하는 닫는 느낌을 준다 */
static const Sequence_t SEQ_DONG = {
    .notes = {
        { NOTE_D5, 120, 3 },
        { NOTE_B4, 200, 2 },
    },
    .count = 2,
    .loop  = 1
};

/* 비상 경보: 도착음과 같은 두 음을 묵음 구간을 두고 3번 반복해 주의를 끈다 */
static const Sequence_t SEQ_EMERGENCY = {
    .notes = {
        { NOTE_E5, 200, 1 },
        { NOTE_D5, 180, 2 },
    },
    .count = 2,
    .loop  = 3
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

/* 노트 하나를 소리내기 시작한다.
 * ARR로 음 높이를, CCR1(듀티)로 음량을 만든다. 듀티 50%(ARR/2)에서 시작해
 * Buzzer_Update()가 CCR을 조금씩 깎으며 여운(감쇠)을 만든다. */
static void play_note(const Note_t *n)
{
    __HAL_TIM_SET_AUTORELOAD(&htim2, n->arr);
    s_ccr_cur = n->arr / 2;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, s_ccr_cur);
    /* 카운터가 새 ARR보다 큰 상태로 남아 있으면 첫 주기가 길게 늘어져
     * "툭" 하는 잡음이 나므로 0으로 리셋한다 */
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

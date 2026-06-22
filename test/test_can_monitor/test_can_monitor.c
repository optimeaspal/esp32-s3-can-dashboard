/*
 * Unit-Tests für die Pro-ID-CAN-Monitor-Tabelle.
 * Native: pio test -e native
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"

#include "../../src/app/can_monitor.h"
#include "../../src/app/can_monitor.c"

static can_monitor_t m;

void setUp(void)    { can_monitor_reset(&m); }
void tearDown(void) {}

/* Mehrere Frames derselben ID → ein Eintrag, letzte Daten + Zähler stimmen. */
void test_same_id_aggregates(void)
{
    uint8_t d1[] = {0x01, 0x02};
    uint8_t d2[] = {0x03, 0x04};
    can_monitor_record(&m, 0x100, false, d1, 2, 1000);
    can_monitor_record(&m, 0x100, false, d2, 2, 2000);

    TEST_ASSERT_EQUAL_UINT(1, m.count);
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    size_t n = can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT32(0x100, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(2, out[0].count);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, out[0].data[1]);
    TEST_ASSERT_EQUAL_INT64(2000, out[0].last_us);
}

/* Verschiedene IDs → Snapshot sortiert aufsteigend nach ID. */
void test_distinct_ids_sorted(void)
{
    uint8_t d[] = {0xAA};
    can_monitor_record(&m, 0x200, false, d, 1, 10);
    can_monitor_record(&m, 0x100, false, d, 1, 20);
    can_monitor_record(&m, 0x180, false, d, 1, 30);

    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    size_t n = can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT32(0x100, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(0x180, out[1].id);
    TEST_ASSERT_EQUAL_UINT32(0x200, out[2].id);
}

/* Volle Tabelle: weitere neue ID wird verworfen, vorhandene weiter gezählt. */
void test_table_full_drops_new_ids(void)
{
    uint8_t d[] = {0x00};
    for (uint32_t i = 0; i < CAN_MONITOR_MAX_IDS; i++)
        can_monitor_record(&m, 0x100 + i, false, d, 1, i);
    TEST_ASSERT_EQUAL_UINT(CAN_MONITOR_MAX_IDS, m.count);

    can_monitor_record(&m, 0x999, false, d, 1, 9999);          /* neue ID */
    TEST_ASSERT_EQUAL_UINT(CAN_MONITOR_MAX_IDS, m.count);

    can_monitor_record(&m, 0x100, false, d, 1, 8888);          /* vorhandene */
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(2, out[0].count);                 /* 0x100 zweimal */
}

/* dlc > 8 wird auf 8 begrenzt (kein Überlauf). */
void test_dlc_clamped(void)
{
    uint8_t d[8] = {1,2,3,4,5,6,7,8};
    can_monitor_record(&m, 0x100, false, d, 200, 1);
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT8(8, out[0].dlc);
}

/* fps: vor 1 s keine Änderung, nach 1 s window_count → fps, dann Reset. */
void test_fps_window(void)
{
    uint8_t d[] = {0x00};
    for (int i = 0; i < 5; i++)
        can_monitor_record(&m, 0x100, false, d, 1, 100);

    can_monitor_update_fps(&m, 500000);     /* 0,5 s → noch nichts */
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(0, out[0].fps);

    can_monitor_update_fps(&m, 1000000);    /* 1,0 s → fps = 5 */
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(5, out[0].fps);
    TEST_ASSERT_EQUAL_UINT32(5, can_monitor_total_fps(&m));

    can_monitor_record(&m, 0x100, false, d, 1, 1000100);  /* neues Fenster */
    can_monitor_update_fps(&m, 2000000);
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].fps);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_same_id_aggregates);
    RUN_TEST(test_distinct_ids_sorted);
    RUN_TEST(test_table_full_drops_new_ids);
    RUN_TEST(test_dlc_clamped);
    RUN_TEST(test_fps_window);
    return UNITY_END();
}

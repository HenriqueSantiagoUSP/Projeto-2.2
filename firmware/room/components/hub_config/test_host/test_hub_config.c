/*
 * Host tests of the configuration JSON (config/desejada, config/aplicada):
 * build and run with run_tests.sh. Needs the cJSON sources the component manager
 * downloads on the first idf.py build.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hub_config.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

static const char *const DOORS[] = { "quarto", "banheiro" };

static rules_params_t s_base;
static rules_params_t s_out;
static char s_err[96];

static void setup(void){
    rules_default_params(&s_base);
    s_base.door_role[0] = DOOR_ROLE_EXIT;
    s_base.door_role[1] = DOOR_ROLE_BATHROOM;
    memset(&s_out, 0xAB, sizeof(s_out));   /* to see whether a refusal touched it */
    s_err[0] = '\0';
}

static bool parse(const char *json){
    return hub_config_from_json(json, strlen(json), &s_base, DOORS, 2, &s_out, s_err, sizeof(s_err));
}

static bool untouched(void){
    rules_params_t pattern;
    memset(&pattern, 0xAB, sizeof(pattern));
    return memcmp(&pattern, &s_out, sizeof(s_out)) == 0;
}

static void test_partial_keeps_the_rest(void){
    setup();
    CHECK(parse("{\"saida_noturna_min\":2,\"banheiro_min\":3}"));
    CHECK(s_out.night_exit_min == 2);
    CHECK(s_out.bathroom_min == 3);
    CHECK(s_out.inactivity_min == s_base.inactivity_min);
    CHECK(s_out.temp_max_c == s_base.temp_max_c);
    CHECK(s_out.door_role[0] == DOOR_ROLE_EXIT);
}

static void test_night_and_roles(void){
    setup();
    CHECK(parse("{\"noite\":{\"inicio\":\"23:30\",\"fim\":\"05:15\"},\"portas\":{\"banheiro\":\"outra\"}}"));
    CHECK(s_out.night_start_min == 23 * 60 + 30);
    CHECK(s_out.night_end_min == 5 * 60 + 15);
    CHECK(s_out.door_role[1] == DOOR_ROLE_OTHER);
    CHECK(s_out.door_role[0] == DOOR_ROLE_EXIT);
}

static void test_temperature_block(void){
    setup();
    CHECK(parse("{\"temperatura\":{\"min\":17.5,\"max\":29,\"histerese\":1,\"persistencia_min\":5}}"));
    CHECK(s_out.temp_min_c == 17.5f);
    CHECK(s_out.temp_max_c == 29.0f);
    CHECK(s_out.temp_hysteresis_c == 1.0f);
    CHECK(s_out.temp_persist_min == 5);
}

static void test_refusals_leave_output_untouched(void){
    const char *bad[] = {
        "{\"saida_noturna_min\":0}",
        "{\"banheiro_min\":1.5}",
        "{\"inatividade_min\":\"120\"}",
        "{\"portas\":{\"cozinha\":\"saida\"}}",
        "{\"portas\":{\"quarto\":\"janela\"}}",
        "{\"noite\":{\"inicio\":\"25:00\"}}",
        "{\"temperatura\":{\"min\":27,\"max\":28}}",
        "{\"vestivel_offline_s\":5}",
        "{\"v\":2}",
        "{nao e json",
        "[1,2]",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        setup();
        bool accepted = parse(bad[i]);
        CHECK(!accepted);
        CHECK(untouched());
        CHECK(s_err[0] != '\0');
        if (accepted) {
            printf("  accepted: %s\n", bad[i]);
        }
    }
}

static void test_error_names_the_key(void){
    setup();
    CHECK(!parse("{\"saida_noturna_min\":0}"));
    CHECK(strstr(s_err, "saida_noturna_min") != NULL);
    setup();
    CHECK(!parse("{\"portas\":{\"cozinha\":\"saida\"}}"));
    CHECK(strstr(s_err, "cozinha") != NULL);
}

/* What the hub publishes (and saves to NVS) must read back as the same parameters. */
static void test_round_trip(void){
    setup();
    CHECK(parse("{\"noite\":{\"inicio\":\"21:45\",\"fim\":\"07:00\"},\"saida_noturna_min\":7,"
                "\"temperatura\":{\"min\":19,\"max\":27.5,\"histerese\":0.25,\"persistencia_min\":3},"
                "\"portas\":{\"quarto\":\"outra\",\"banheiro\":\"saida\"},\"bateria_baixa_mV\":3600}"));
    rules_params_t first = s_out;

    char *json = hub_config_to_json(&first, DOORS, 2, HUB_CONFIG_MQTT, NULL);
    CHECK(json != NULL);
    CHECK(strstr(json, "\"origem\":\"mqtt\"") != NULL);

    rules_params_t defaults;
    rules_default_params(&defaults);
    rules_params_t second;
    char err[96];
    CHECK(hub_config_from_json(json, strlen(json), &defaults, DOORS, 2, &second, err, sizeof(err)));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    free(json);

    json = hub_config_to_json(&first, DOORS, 2, HUB_CONFIG_DEFAULT, "faixa invalida");
    CHECK(strstr(json, "\"erro\":\"faixa invalida\"") != NULL);
    CHECK(strstr(json, "\"origem\":\"padrao\"") != NULL);
    free(json);
}

int main(void){
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "partial config keeps the rest",       test_partial_keeps_the_rest },
        { "night window and door roles",         test_night_and_roles },
        { "temperature block",                   test_temperature_block },
        { "refusals leave the output untouched", test_refusals_leave_output_untouched },
        { "error names the key",                 test_error_names_the_key },
        { "round trip through config/aplicada",  test_round_trip },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = s_failures;
        tests[i].fn();
        printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

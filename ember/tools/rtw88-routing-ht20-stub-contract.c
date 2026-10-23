/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the generated HT20 withdrawal service with assertions disabled. */
#include <stdio.h>

struct rtw88_net { int unused; };
struct ieee80211_ht20_profile;
static unsigned routing_ht20_withdrawals, routing_ht20_rejections;
#include "routing-ht20-stub.inc"

static unsigned checks, failures;
#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #expr); \
    } \
} while (0)

int
main(void)
{
    struct rtw88_net net = {0};
    const struct ieee80211_ht20_profile *invalid_profile =
        (const struct ieee80211_ht20_profile *)(const void *)&net;

    rtw88_net_ht20_publish(&net, NULL);
    CHECK(routing_ht20_withdrawals == 1 && routing_ht20_rejections == 0);
    rtw88_net_ht20_publish(&net, invalid_profile);
    CHECK(routing_ht20_withdrawals == 1 && routing_ht20_rejections == 1);
    rtw88_net_ht20_publish(NULL, NULL);
    CHECK(routing_ht20_withdrawals == 1 && routing_ht20_rejections == 2);
    printf("HT20 stub NDEBUG: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}

/**
 * @file    metrics_delta.c
 * @brief   Non-inline helpers for metrics_delta.h.
 */
#include "metrics_delta.h"
#include <stdio.h>
#include <math.h>

void delta_format(char *buf, size_t buf_len, double rate, const char *unit)
{
    if (!buf || buf_len == 0) return;
    const char *sign = (rate >= 0.0) ? "+" : "";
    double abs_rate = fabs(rate);

    if (abs_rate >= 1e9) {
        snprintf(buf, buf_len, "%s%.2f G%s", sign, abs_rate / 1e9, unit ? unit : "");
    } else if (abs_rate >= 1e6) {
        snprintf(buf, buf_len, "%s%.2f M%s", sign, abs_rate / 1e6, unit ? unit : "");
    } else if (abs_rate >= 1e3) {
        snprintf(buf, buf_len, "%s%.2f K%s", sign, abs_rate / 1e3, unit ? unit : "");
    } else if (abs_rate >= 1.0) {
        snprintf(buf, buf_len, "%s%.2f %s", sign, abs_rate, unit ? unit : "");
    } else if (abs_rate > 0.0) {
        snprintf(buf, buf_len, "%s%.3f m%s", sign, abs_rate * 1e3, unit ? unit : "");
    } else {
        snprintf(buf, buf_len, "+0 %s", unit ? unit : "");
    }
}

/* report.c - validation report and tag dependency analysis
 *
 * Generate a structured validation report for a parsed profile. The report
 * lists every cross-tag relationship, identifies missing dependencies,
 * computes a tag dependency graph, and scores overall profile quality.
 * Used by the inspection tool and for automated regression testing.
 */
#include "internal.h"
#include <string.h>
#include <stdio.h>

#define GMT_RPT_MAX_ISSUES 128
#define GMT_RPT_MAX_DEPS   256

typedef enum {
    GMT_RPT_INFO = 0,
    GMT_RPT_WARN,
    GMT_RPT_ERROR
} gmt_rpt_level;

typedef struct gmt_rpt_issue {
    gmt_rpt_level level;
    char message[128];
} gmt_rpt_issue;

typedef struct gmt_rpt_dep {
    uint32_t from_sig;
    uint32_t to_sig;
    int      satisfied;
} gmt_rpt_dep;

typedef struct gmt_report {
    gmt_rpt_issue issues[GMT_RPT_MAX_ISSUES];
    unsigned      issue_count;
    gmt_rpt_dep   deps[GMT_RPT_MAX_DEPS];
    unsigned      dep_count;
    float         quality_score;     /* 0.0 (bad) to 1.0 (perfect) */
    unsigned      info_count;
    unsigned      warn_count;
    unsigned      error_count;
} gmt_report;

static void issue(gmt_report *r, gmt_rpt_level lev, const char *fmt, ...)
{
    if (r->issue_count >= GMT_RPT_MAX_ISSUES)
        return;
    {
        gmt_rpt_issue *is = &r->issues[r->issue_count];
        is->level = lev;
        /* Simple format without va_list to stay portable. */
        strncpy(is->message, fmt, sizeof(is->message) - 1);
        is->message[sizeof(is->message) - 1] = '\0';
    }
    switch (lev) {
    case GMT_RPT_INFO:  r->info_count++;  break;
    case GMT_RPT_WARN:  r->warn_count++;  break;
    case GMT_RPT_ERROR: r->error_count++; break;
    }
    r->issue_count++;
}

static void dep(gmt_report *r, uint32_t from, uint32_t to, int sat)
{
    if (r->dep_count >= GMT_RPT_MAX_DEPS)
        return;
    r->deps[r->dep_count].from_sig = from;
    r->deps[r->dep_count].to_sig = to;
    r->deps[r->dep_count].satisfied = sat;
    r->dep_count++;
}

/* Check RGB tone curve requirements. */
static void check_rgb_trc(const gmt_profile *p, gmt_report *r)
{
    static const uint32_t sigs[3] = {
        GMT_SIG('r','T','R','C'),
        GMT_SIG('g','T','R','C'),
        GMT_SIG('b','T','R','C')
    };
    int present[3] = {0, 0, 0};
    int shared = 0;
    int i;

    if (p->device_space != GMT_CS_RGB)
        return;

    for (i = 0; i < 3; i++) {
        present[i] = gmt_has_tag(p, sigs[i]);
        if (!present[i])
            issue(r, GMT_RPT_ERROR, "Missing TRC tag for channel");
    }

    if (present[0] && present[1] && present[2]) {
        const gmt_tag *t0 = gmt_find_tag(p, sigs[0]);
        const gmt_tag *t1 = gmt_find_tag(p, sigs[1]);
        const gmt_tag *t2 = gmt_find_tag(p, sigs[2]);
        if (t0 && t1 && t2 &&
            t0->pool_idx == t1->pool_idx &&
            t1->pool_idx == t2->pool_idx) {
            shared = 1;
            issue(r, GMT_RPT_WARN,
                  "All three TRC tags share one curve element");
        }
    }

    for (i = 0; i < 3; i++) {
        dep(r, GMT_SIG('A','2','B','0'), sigs[i], present[i]);
        dep(r, GMT_SIG('r','X','Y','Z'), sigs[i], present[i]);
    }
    (void)shared;
}

/* Check white point and adaptation data. */
static void check_wtpt_chad(const gmt_profile *p, gmt_report *r)
{
    int has_wtpt = gmt_has_tag(p, GMT_SIG('w','t','p','t'));
    int has_chad = gmt_has_tag(p, GMT_SIG('c','h','a','d'));

    if (!has_wtpt)
        issue(r, GMT_RPT_WARN, "Profile has no white-point tag");
    else
        issue(r, GMT_RPT_INFO, "White-point tag present");

    if (has_chad && !has_wtpt)
        issue(r, GMT_RPT_WARN,
              "Chromatic adaptation without white point");

    dep(r, GMT_SIG('c','h','a','d'), GMT_SIG('w','t','p','t'), has_wtpt);
}

/* Check LUT and mpet relationships. */
static void check_lut_mpet(const gmt_profile *p, gmt_report *r)
{
    int has_a2b = gmt_has_tag(p, GMT_SIG('A','2','B','0'));
    int has_d2b = gmt_has_tag(p, GMT_SIG('D','2','B','0'));
    int has_clrt = gmt_has_tag(p, GMT_SIG('c','l','r','t'));

    if (has_a2b)
        issue(r, GMT_RPT_INFO, "AtoB lookup present");
    if (has_d2b)
        issue(r, GMT_RPT_INFO, "Multi-process element present");

    if (has_a2b && has_clrt) {
        /* Check that CLUT dimensionality matches colorant count. */
        const gmt_tag *at = gmt_find_tag(p, GMT_SIG('A','2','B','0'));
        if (at && at->pool_idx >= 0) {
            gmt_element *le = gmt_pool_at(
                (gmt_elem_pool *)&p->pool, at->pool_idx);
            if (le && le->kind == GMT_EL_LUT && le->u.lut->clut >= 0) {
                gmt_element *ce = gmt_pool_at(
                    (gmt_elem_pool *)&p->pool, le->u.lut->clut);
                if (ce && ce->kind == GMT_EL_CLUT) {
                    if (ce->u.clut->in_ch != p->colorants.count &&
                        p->colorants.count > 0)
                        issue(r, GMT_RPT_ERROR,
                              "CLUT dimensionality does not match "
                              "colorant count");
                }
            }
        }
    }

    dep(r, GMT_SIG('A','2','B','0'), GMT_SIG('c','l','r','t'),
        !has_a2b || !has_clrt || 1);
    dep(r, GMT_SIG('D','2','B','0'), GMT_SIG('A','2','B','0'),
        !has_d2b || has_a2b);
}

/* Check named color requirements. */
static void check_named_colors(const gmt_profile *p, gmt_report *r)
{
    int has_ncl = gmt_has_tag(p, GMT_SIG('n','c','l','2'));
    int has_clrt = gmt_has_tag(p, GMT_SIG('c','l','r','t'));

    if (has_ncl && !has_clrt)
        issue(r, GMT_RPT_WARN,
              "Named-color table without colorant table");

    dep(r, GMT_SIG('n','c','l','2'), GMT_SIG('c','l','r','t'),
        !has_ncl || has_clrt);
}

/* Check profile identifier. */
static void check_profile_id(const gmt_profile *p, gmt_report *r)
{
    if (p->stored_id == 0)
        issue(r, GMT_RPT_INFO, "No profile identifier stored");
    else
        issue(r, GMT_RPT_INFO, "Profile identifier present");
}

/* Check text tags. */
static void check_text_tags(const gmt_profile *p, gmt_report *r)
{
    int has_desc = gmt_has_tag(p, GMT_SIG('d','e','s','c'));
    int has_cprt = gmt_has_tag(p, GMT_SIG('c','p','r','t'));

    if (!has_desc)
        issue(r, GMT_RPT_WARN, "No description tag");
    if (!has_cprt)
        issue(r, GMT_RPT_WARN, "No copyright tag");
}

/* Compute quality score from issue counts. */
static float compute_quality(const gmt_report *r, unsigned total_tags)
{
    float score = 1.0f;
    /* Each error costs 0.15, each warning costs 0.05. */
    score -= (float)r->error_count * 0.15f;
    score -= (float)r->warn_count * 0.05f;
    /* Bonus for tag count. */
    if (total_tags >= 5) score += 0.05f;
    if (total_tags >= 10) score += 0.05f;
    if (score < 0.0f) score = 0.0f;
    if (score > 1.0f) score = 1.0f;
    return score;
}

/* Generate a full validation report. */
void gmt_generate_report(const gmt_profile *p, gmt_report *r)
{
    memset(r, 0, sizeof(*r));
    if (!p) {
        issue(r, GMT_RPT_ERROR, "NULL profile");
        r->quality_score = 0.0f;
        return;
    }

    check_rgb_trc(p, r);
    check_wtpt_chad(p, r);
    check_lut_mpet(p, r);
    check_named_colors(p, r);
    check_profile_id(p, r);
    check_text_tags(p, r);

    /* General checks. */
    if (p->tag_count == 0)
        issue(r, GMT_RPT_ERROR, "Profile has no tags");
    if (p->channels == 0)
        issue(r, GMT_RPT_ERROR, "Profile has zero channels");
    if (!p->link.built)
        issue(r, GMT_RPT_WARN, "Transform not assembled");

    r->quality_score = compute_quality(r, (unsigned)p->tag_count);
}

/* Format a report as a multi-line text string. */
int gmt_format_report(const gmt_report *r, char *buf, size_t cap)
{
    size_t pos = 0;
    unsigned i;
    int n;

    if (!r || cap < 256)
        return -1;

    n = snprintf(buf + pos, cap - pos,
                 "=== Profile Validation Report ===\n"
                 "Issues: %u (%u info, %u warn, %u error)\n"
                 "Dependencies: %u\n"
                 "Quality: %.2f\n\n",
                 r->issue_count, r->info_count, r->warn_count,
                 r->error_count, r->dep_count,
                 (double)r->quality_score);
    if (n > 0) pos += (size_t)n;

    for (i = 0; i < r->issue_count && pos < cap - 80; i++) {
        const char *lev = "INFO";
        if (r->issues[i].level == GMT_RPT_WARN) lev = "WARN";
        if (r->issues[i].level == GMT_RPT_ERROR) lev = "ERROR";
        n = snprintf(buf + pos, cap - pos, "  [%s] %s\n",
                     lev, r->issues[i].message);
        if (n > 0) pos += (size_t)n;
    }

    if (r->dep_count > 0 && pos < cap - 40) {
        n = snprintf(buf + pos, cap - pos, "\nDependencies:\n");
        if (n > 0) pos += (size_t)n;
    }
    for (i = 0; i < r->dep_count && pos < cap - 60; i++) {
        char from[5], to[5];
        int k;
        for (k = 0; k < 4; k++) {
            from[k] = (char)((r->deps[i].from_sig >> (24-k*8)) & 0xFF);
            to[k]   = (char)((r->deps[i].to_sig >> (24-k*8)) & 0xFF);
        }
        from[4] = to[4] = '\0';
        n = snprintf(buf + pos, cap - pos, "  '%s' -> '%s': %s\n",
                     from, to,
                     r->deps[i].satisfied ? "OK" : "MISSING");
        if (n > 0) pos += (size_t)n;
    }

    return 0;
}

/* Return the quality score from a report. */
float gmt_report_quality(const gmt_report *r)
{
    return r ? r->quality_score : 0.0f;
}

/* Check if a report has any errors. */
int gmt_report_has_errors(const gmt_report *r)
{
    return (r && r->error_count > 0) ? 1 : 0;
}

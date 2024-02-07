#include "vapi/vapi_options.h"
#include "vut_options.h"

#define JSON_OPT_p                                                      \
        VOPT("p", "[-p]", "Pretty-print",                               \
            "Pretty-print transactions rather than using NDJSON"        \
        )
VSL_OPT_b
VSL_OPT_c
VSL_OPT_C
VUT_OPT_d
VUT_GLOBAL_OPT_D
VSL_OPT_E
VUT_OPT_g
VUT_OPT_h
VSL_OPT_i
VSL_OPT_I
VUT_OPT_k
VSL_OPT_L
VUT_OPT_n
JSON_OPT_p
VUT_GLOBAL_OPT_P
#ifdef VUT_OPT_Q
VUT_OPT_Q
#endif
VUT_OPT_q
VUT_OPT_r
VSL_OPT_R
VUT_OPT_t
VSL_OPT_T
VUT_GLOBAL_OPT_V
VSL_OPT_x
VSL_OPT_X

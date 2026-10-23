#include "tda/core/status.h"

#include "tda/core/util.h"

const char *tda_status_to_str(tda_Status st) {
    switch (st) {
        case TDA_STATUS_OK:
            return TDA_STRINGIFY(TDA_STATUS_OK);
        case TDA_STATUS_ERR_NO_MEM:
            return TDA_STRINGIFY(TDA_STATUS_ERR_NO_MEM);
    }

    return "UNKNOWN_TDA_STATUS";
}

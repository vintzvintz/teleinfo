
#include "decode.h"

typedef struct flagdef_s{
    tic_char_t label[TIC_SIZE_ETIQUETTE];
    tic_dataset_flags_t flags;
} flagdef_t;

#define TIC_DS_PUBLISHED       (1 << 0)
#define TIC_DS_HAS_TIMESTAMP   (1 << 1)
#define TIC_DS_NUMERIQUE       (1 << 2)

tic_error_t tic_get_flags( const tic_char_t *etiquette, tic_dataset_flags_t *flags );

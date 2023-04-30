
#pragma once


// taille des buffers
#define TIC_SIZE_ETIQUETTE    16     // etiquette
#define TIC_SIZE_VALUE        128    // donnée ou horodate
#define TIC_SIZE_CHECKSUM     4      // checksum

// nb maxi de datasets dans une trame
// #define TIC_MAX_DATASETS    99

typedef char tic_char_t;
typedef uint32_t tic_dataset_flags_t;

typedef struct flagdef_s {
    tic_char_t label[TIC_SIZE_ETIQUETTE];
    tic_dataset_flags_t flags;
} flags_definition_t;

// TODO remplacer par une enum
#define TIC_DS_PUBLISHED       (1 << 0)
#define TIC_DS_HAS_TIMESTAMP   (1 << 1)
#define TIC_DS_NUMERIQUE       (1 << 2)


//tic_dataset_t est une liste de données décodées = contenu d'une trame complete
typedef struct dataset_s {
    tic_char_t etiquette[TIC_SIZE_ETIQUETTE];
    tic_char_t horodate[TIC_SIZE_VALUE];
    tic_char_t valeur[TIC_SIZE_VALUE];
    tic_dataset_flags_t flags;
    struct dataset_s *next;                    // linked_list
} dataset_t;

 /*
 * Utilisation des datasets par d'autres tâches
 */

dataset_t * dataset_alloc();

void dataset_free( dataset_t *dataset );

tic_error_t dataset_print( const dataset_t *dataset );

uint32_t dataset_count( dataset_t *dataset );

// recherche un element selon son etiquette
const dataset_t* dataset_find( const dataset_t *ds, const char *etiquette );

// tri selon les étiquettes
dataset_t * dataset_sort( dataset_t *ds);

// insere ds dans sorted avec tri selon les etiquettes
dataset_t * dataset_insert( dataset_t *sorted, dataset_t *ds);

// ajoute ds après le dernier elements de append_to
dataset_t * dataset_append( dataset_t *append_to, dataset_t*ds );

tic_error_t dataset_flags_definition( const tic_char_t *etiquette, tic_dataset_flags_t *flags );

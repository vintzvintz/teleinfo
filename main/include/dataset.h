
#pragma once

#include "tic_types.h"




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

// trouve les flags associés à une donnée
tic_error_t dataset_flags_definition (const tic_char_t *etiquette, tic_mode_t mode, tic_dataset_flags_t *out_flags);

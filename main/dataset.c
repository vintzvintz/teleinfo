
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "tic_types.h"


static const char *TAG = "dataset.c";

#define TEXTE (TIC_DS_PUBLISHED)
#define NUMERIQUE (TIC_DS_PUBLISHED|TIC_DS_NUMERIQUE)
#define TEXTE_TS (TEXTE|TIC_DS_HAS_TIMESTAMP)
#define NUMERIQUE_TS (NUMERIQUE|TIC_DS_HAS_TIMESTAMP)
#define IGNORE 0


static const flags_definition_t TIC_DATA_STANDARD[] = {
    {.label="ADSC",    .flags=TEXTE},              // numero de serie du compteur
    {.label="EAST",    .flags=NUMERIQUE},          // energie active soutirée
    {.label="IRMS1",   .flags=NUMERIQUE},          // intensite instantanée
    {.label="URMS1",   .flags=NUMERIQUE},          // tension instantanée
    {.label="SINSTS",  .flags=NUMERIQUE},          // Puissance apparente instantanée
    {.label="STGE",    .flags=TEXTE},              // flags d'état   
    {.label="DATE",    .flags=TEXTE_TS},           // heure et date courante (sans données)
    {.label="CCASN",   .flags=NUMERIQUE_TS},       // courbe de charge de la periode N (pas 30 minutes)
    {.label="CCASN-1", .flags=NUMERIQUE_TS},       // courbe de charge de la période N-1 (pas 30 minutes)
    {.label="SMAXSN",  .flags=NUMERIQUE_TS},       // puissance apparente maxi du jour en cours
    {.label="SMAXSN-1",.flags=NUMERIQUE_TS},       // puissannce apparente maxi de la veille
    {.label="UMOY1",   .flags=NUMERIQUE_TS},       // tension moyenne ( pas 10 minutes )
    {.label="EASD01",  .flags=IGNORE},             // index distributeur
    {.label="EASD02",  .flags=IGNORE},
    {.label="EASD03",  .flags=IGNORE},
    {.label="EASD04",  .flags=IGNORE},
    {.label="EASF01",  .flags=IGNORE},             // index fournisseur
    {.label="EASF02",  .flags=IGNORE},
    {.label="EASF03",  .flags=IGNORE},
    {.label="EASF04",  .flags=IGNORE},
    {.label="EASF04",  .flags=IGNORE},
    {.label="EASF05",  .flags=IGNORE},
    {.label="EASF06",  .flags=IGNORE},
    {.label="EASF07",  .flags=IGNORE},
    {.label="EASF08",  .flags=IGNORE},
    {.label="EASF09",  .flags=IGNORE},
    {.label="EASF10",  .flags=IGNORE}, 
    {.label="LTARF",   .flags=IGNORE},      // libellé tarif fournisseur en cours
    {.label="MSG1",    .flags=IGNORE},      // message court
    {.label="MSG2",    .flags=IGNORE},      // message ultra-court
    {.label="NGTF",    .flags=IGNORE},      // nom calendrier fournisseur
    {.label="NJOURF",  .flags=IGNORE},      // numero jour en cours calendrier fournisseur
    {.label="NJOURF+1",.flags=IGNORE},      // numero prochain jour calendrier fournisseur
    {.label="NTARF",   .flags=IGNORE},      // numero index tarifaire en cours
    {.label="PCOUP",   .flags=IGNORE},      // puissance coupure
    {.label="PJOURF+1",.flags=IGNORE},      // Profil prochain jour calendrier fournisseur
    {.label="PREF",    .flags=IGNORE},      // Puissance apparente de référence
    {.label="PRM",     .flags=IGNORE},      // numéo PRM ou PDL ( référence enedis )
    {.label="RELAIS",  .flags=IGNORE},      // etat des relais
    {.label="VTIC",    .flags=IGNORE},      // version de la TIC
};

static const flags_definition_t TIC_DATA_HISTORIQUE[] = {    
    {.label="ADCO",    .flags=TEXTE},       // numero de serie du compteur
    {.label="OPTARIF", .flags=TEXTE},       // energie active soutirée
    {.label="ISOUSC",  .flags=NUMERIQUE},   // intensite souscrite
    {.label="BASE",    .flags=NUMERIQUE},   // index d'energie en tarif de base
    {.label="HCHC",    .flags=NUMERIQUE},   // index d'energie
    {.label="HCHP",    .flags=NUMERIQUE},   // index d'energie
    {.label="PTEC",    .flags=TEXTE},       // intensite apparente instantanée
    {.label="IINST",   .flags=NUMERIQUE},   // intensite apparente instantanée
    {.label="IMAX",    .flags=NUMERIQUE},   // intensité max
    {.label="PAPP",    .flags=NUMERIQUE},   // puissance apparente instantanée
    {.label="MOTDETAT",.flags=TEXTE}
};

#define TIC_DATA_STANDARD_COUNT   (sizeof(TIC_DATA_STANDARD) / sizeof(TIC_DATA_STANDARD[0]))
#define TIC_DATA_HISTORIQUE_COUNT (sizeof(TIC_DATA_HISTORIQUE) / sizeof(TIC_DATA_HISTORIQUE[0]))


static tic_error_t flags_lookup ( const tic_char_t *etiquette, 
                                  const flags_definition_t *defs,
                                  size_t defs_len, 
                                  tic_dataset_flags_t *out_flags )
{
    assert(out_flags);
    for( size_t i=0; i< defs_len; i++ )
    {
        if( strncmp( defs[i].label, etiquette, TIC_SIZE_ETIQUETTE ) == 0 )
        {
            *out_flags = defs[i].flags;
            return TIC_OK;
        }
    }
    return TIC_ERR_UNKNOWN_DATA;
}

/// @brief renvoie les attributs texte/numerique et avec/sans horodate pour une donnée TIC
/// @param etiquette nom de la donnée
/// @param mode utiliser la table standard ou historique ( ou 0 pour chercher dans toutes les tables)
/// @param out_flags resultat
/// @return TIC_OK si l'etiquette est trouvée, TIC_ERR_UNKNOWN_DATA sinon
tic_error_t dataset_flags_definition ( const tic_char_t *etiquette, tic_mode_t mode, tic_dataset_flags_t *out_flags)
{
    if ((!mode) || (mode == TIC_MODE_HISTORIQUE))
    {
        if( flags_lookup (etiquette, TIC_DATA_HISTORIQUE, TIC_DATA_HISTORIQUE_COUNT, out_flags) == TIC_OK )
        {
            return TIC_OK;
        }
    }

    if( (!mode) || (mode == TIC_MODE_STANDARD))
    {
        if (flags_lookup (etiquette, TIC_DATA_STANDARD, TIC_DATA_STANDARD_COUNT, out_flags) == TIC_OK)
        {
            return TIC_OK;
        }
    }
    return TIC_ERR_UNKNOWN_DATA;
}


uint32_t dataset_count( dataset_t *dataset )
{
    uint32_t nb = 0;
    while ( dataset != NULL )
    {
        nb++;
        dataset = dataset->next;
    }
    return nb;
}


tic_error_t dataset_print( const dataset_t *ds )
{
    // ESP_LOGD( TAG, "print_datasets()");
    char flags_str[3];
    while( ds != NULL )
    {
        flags_str[0]= '.';
        flags_str[1]= '.';
        flags_str[2]=0;    // null-terminated
        if( ds->flags & TIC_DS_PUBLISHED )
        {
            flags_str[0]= ( ds->flags & TIC_DS_NUMERIQUE ) ? 'N' : 'S';
            if( ds->flags & TIC_DS_HAS_TIMESTAMP )
                flags_str[1]= 'H';
        }
        ESP_LOGD( TAG, "%8.8s %s %s %s", ds->etiquette, flags_str, ds->horodate, ds->valeur );
        ds = ds->next;
    }
    return TIC_OK;
}


// Statically allocate and initialize the spinlock
//static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

dataset_t * dataset_alloc()
{
//    ESP_LOGD( TAG, "datasets_alloc()");

    dataset_t *ds =  calloc(1, sizeof(dataset_t));

    // juste pour le debug
    /*
    if( ds!=NULL )
    {
        taskENTER_CRITICAL(&my_spinlock);
        s_allocated_datasets += 1;
        taskEXIT_CRITICAL(&my_spinlock);
    }
    */
    return ds;
}


void dataset_free( dataset_t *ds )
{
    /*
    ESP_LOGD( TAG, "dataset_free(%p)", ds );
    //int32_t nb_init = dataset_count(ds);
    //int32_t nb_alloc_before = 0;
    //nb_alloc_before = s_allocated_datasets;
    dataset_t *ds_init = ds;   // juste pour le log_debug
    */

    int32_t nb_free = 0;
    while ( ds != NULL )
    {
        dataset_t *tmp = ds;
        ds = ds->next;
        free( tmp );
//        s_allocated_datasets -= 1;
        nb_free += 1;
    }

    // juste pour le debug
    /*
    taskENTER_CRITICAL(&my_spinlock);
    s_allocated_datasets -= nb_free;
    taskEXIT_CRITICAL(&my_spinlock);
    ESP_LOGD( TAG, "dataset_free(%p) %"PRIi32"/%"PRIi32" libérés. Reste %"PRIi32"/%"PRIi32" datasets alloues", ds_init, nb_free, nb_init, s_allocated_datasets, nb_alloc_before );
    */
}


dataset_t* dataset_insert( dataset_t *sorted, dataset_t *ds)
{
    assert( ds != NULL );           // l'insertion de NULL est invalide
    assert( ds->next == NULL );     // ds doit être un element isolé, le ptr sur le suivant doit rester chez l'appelant

    dataset_t *item = sorted;

    while( item != NULL )
    {
        if( strcmp( ds->etiquette, item->etiquette ) < 0 )
        {
            // ds est plus petit que l'élément courant,
            assert( item == sorted ); // impossible sauf pour le premier element de la liste triée
            ds->next = item;          //  insere avant
            sorted = ds;
            break;
        }
        if ( (item->next == NULL) || (strcmp( ds->etiquette, item->next->etiquette ) <= 0 ) )
        {
            // ds est plus petit que l'élément suivant (ou pas d'élement suivant)
            ds->next = item->next;    // insere après
            item->next = ds;
            break;
        }
        item = item->next;
    }

    if( sorted == NULL )
    {
        //ESP_LOGD( TAG, "la liste triée est vide, renvoie %s", ds->etiquette );
        sorted = ds;
    }
    return sorted;
}

// ajoute ds après le dernier elements de append_to
dataset_t * dataset_append( dataset_t *append_to, dataset_t*ds )
{
    // impossible d'ajouter au pointeur null alors on renvoie juste ds
    if( append_to == NULL )
    {
        return ds;
    }
    // cherche le dernier element de append_to
    dataset_t *tail = append_to;
    while ( tail->next != NULL )
    {
        tail=tail->next;
    }
    tail->next = ds;     // ajoute ds
    return append_to;
}


dataset_t * dataset_sort(dataset_t *ds)
{
    dataset_t * sorted = NULL;
    dataset_t * ds_next = NULL;

    while( ds != NULL )
    {
        // copie le ptr vers l'item suivant car ds->next va être modifié lors de son insertion dans 'sorted'
        ds_next = ds->next;
        ds->next=NULL;

        sorted = dataset_insert( sorted, ds );
        ds = ds_next;
    }
    return sorted;
}


const dataset_t* dataset_find( const dataset_t *ds, const char *etiquette )
{
    while( ds != NULL )
    {
        if( strncmp( ds->etiquette, etiquette, TIC_SIZE_ETIQUETTE ) == 0 )
        {
            return ds;
        }
        ds = ds->next;
    }
    return NULL;
}


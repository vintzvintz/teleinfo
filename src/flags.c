

// a incluce avant tic_decode.h
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"


#include <stdio.h>
#include <string.h>
#include "decode.h"
#include "flags.h"

#define TEXTE (TIC_DS_PUBLISHED)
#define NUMERIQUE (TIC_DS_PUBLISHED|TIC_DS_NUMERIQUE)
#define TEXTE_TS (TEXTE|TIC_DS_HAS_TIMESTAMP)
#define NUMERIQUE_TS (NUMERIQUE|TIC_DS_HAS_TIMESTAMP)
#define IGNORE 0


static const flagdef_t TIC_DATA_DEFINITION[] = {
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

#define DEF_CNT (sizeof(TIC_DATA_DEFINITION) / sizeof(TIC_DATA_DEFINITION[0]))

tic_error_t tic_get_flags( const tic_char_t *etiquette, tic_dataset_flags_t *flags )
{
    for( size_t i=0; i< DEF_CNT; i++ )
    {
        if( strncmp( TIC_DATA_DEFINITION[i].label, etiquette, TIC_SIZE_ETIQUETTE ) == 0 )
        {
            *flags = TIC_DATA_DEFINITION[i].flags;
            return TIC_OK;
        }
    }
    return TIC_ERR_UNKNOWN_DATA;
}

/*
 * exemple de trame complete

I (3507707) mqtt_task: Topic=home/elec/042262019986
I (3508597) tic_decode: ADSC    042262019986
I (3508597) tic_decode: CCASN   E230403110000   00580
I (3508607) tic_decode: CCASN-1 E230403103000   00880
I (3508607) tic_decode: DATE    E230403112345
I (3508607) tic_decode: EASD01  003072794
I (3508617) tic_decode: EASD02  000000000
I (3508617) tic_decode: EASD03  000000000
I (3508627) tic_decode: EASD04  000000000
I (3508627) tic_decode: EASF01  003072794
I (3508637) tic_decode: EASF02  000000000
I (3508637) tic_decode: EASF03  000000000
I (3508647) tic_decode: EASF04  000000000
I (3508647) tic_decode: EASF05  000000000
I (3508657) tic_decode: EASF06  000000000
I (3508657) tic_decode: EASF07  000000000
I (3508657) tic_decode: EASF08  000000000
I (3508667) tic_decode: EASF09  000000000
I (3508667) tic_decode: EASF10  000000000
I (3508677) tic_decode: EAST    003072794
I (3508677) tic_decode: IRMS1   003
I (3508687) tic_decode: LTARF         BASE      
I (3508687) tic_decode: MSG1    PAS DE          MESSAGE         
I (3508697) tic_decode: NGTF          BASE      
I (3508697) tic_decode: NJOURF  00
I (3508707) tic_decode: NJOURF+1        00
I (3508707) tic_decode: NTARF   01
I (3508717) tic_decode: PCOUP   15
I (3508717) tic_decode: PJOURF+1        00008001 NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE
I (3508727) tic_decode: PREF    15
I (3508737) tic_decode: PRM     19193632354045
I (3508737) tic_decode: RELAIS  000
I (3508737) tic_decode: SINSTS  00805
I (3508747) tic_decode: SMAXSN  E230403074120   03503
I (3508747) tic_decode: SMAXSN-1        E230402071138   09136
I (3508757) tic_decode: STGE    00DA0001
I (3508757) tic_decode: UMOY1   E230403112000   239
I (3508767) tic_decode: URMS1   240
I (3508767) tic_decode: VTIC    02
*/
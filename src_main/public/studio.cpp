#include <stddef.h>
#include <limits.h>
#include "utlvector.h"
#include "studio.h"
#include "filesystem.h"

#if defined(CLIENT_DLL) || defined(GAME_DLL)
extern IFileSystem *filesystem;
#endif

#define MAX_SHAREDMODELS 256

struct sharedmodeltdata_t
{
    char pFileName[260];
    studiosharehdr_t *pSharedModel;
    bool isSet;
};

static sharedmodeltdata_t hSharedData[MAX_SHAREDMODELS];

studiosharehdr_t *LoadSharedModel( char *pModelName )
{
#if defined(CLIENT_DLL) || defined(GAME_DLL)
    if ( !filesystem || !pModelName || !*pModelName || strlen(pModelName) >= sizeof(hSharedData[0].pFileName) )
        return NULL;

    int freeSlot = -1;
    for ( int i = 0; i < MAX_SHAREDMODELS; ++i )
    {
        if ( hSharedData[i].isSet && !Q_stricmp( pModelName, hSharedData[i].pFileName ) )
            return hSharedData[i].pSharedModel;
        if ( !hSharedData[i].isSet && freeSlot < 0 )
            freeSlot = i;
    }
    if ( freeSlot < 0 )
        return NULL;

    FileHandle_t file = filesystem->Open( pModelName, "rb", "GAME" );
    if ( file == FILESYSTEM_INVALID_HANDLE )
        return NULL;
    unsigned int size = filesystem->Size( file );
    if ( size < sizeof(studiosharehdr_t) || size > INT_MAX )
    {
        filesystem->Close( file );
        return NULL;
    }
    studiosharehdr_t *header = (studiosharehdr_t *)malloc( size );
    if ( !header )
    {
        filesystem->Close( file );
        return NULL;
    }
    int bytesRead = filesystem->Read( header, (int)size, file );
    filesystem->Close( file );
    if ( bytesRead != (int)size || header->id != 1195459657 || header->version != STUDIO_VERSION ||
         header->length < (int)sizeof(studiosharehdr_t) || (unsigned int)header->length > size ||
         header->numanim < 0 || header->animdescindex < 0 ||
         (unsigned int)header->animdescindex > (unsigned int)header->length ||
         (unsigned int)header->numanim > ((unsigned int)header->length - (unsigned int)header->animdescindex) / sizeof(mstudioanimdesc_t) )
    {
        free( header );
        return NULL;
    }
    Q_strncpy( hSharedData[freeSlot].pFileName, pModelName, sizeof(hSharedData[freeSlot].pFileName) );
    hSharedData[freeSlot].pSharedModel = header;
    hSharedData[freeSlot].isSet = true;
    return header;
#else
    return NULL;
#endif
}


inline int GetCurrentOffset( unsigned char *pStarDataPointer, unsigned char *pCurrentDataPointer )
{
	return pCurrentDataPointer - pStarDataPointer;
}

int LoadOldAnimation( const mstudioseqdesc_t *pSeqDesc, int index1, int index2)
{
	return pSeqDesc->pAnimIndexer()->pAnimValue(index1, index2);
}


//For 32 36 version
void ConvertStudioHDRFrom3236To37( studiohdr_t *pStudioHdr )
{
	int offsetend = (int)offsetof(studiohdr_t, animdescindex);

	studiohdr_t *pStudioHdrNew = new studiohdr_t();
	memset( pStudioHdrNew, 0, sizeof(studiohdr_t) );
	memcpy( pStudioHdrNew, pStudioHdr, offsetend + 4);
	memcpy( (char *)pStudioHdrNew + (offsetend + 16), (char *)pStudioHdr + offsetend, sizeof(studiohdr_v36_t) - offsetend );

	pStudioHdrNew->numanimgroup = -1;
	pStudioHdrNew->animgroupindex = -1;
	pStudioHdrNew->numbonedesc = -1;
	pStudioHdrNew->bonedescindex = -1;
	pStudioHdrNew->unused[0] = pStudioHdr->version;

	memcpy( pStudioHdr, pStudioHdrNew, pStudioHdrNew->boneindex );
	delete pStudioHdrNew;

	int version = pStudioHdr->version;
		
	pStudioHdr->version = STUDIO_VERSION;

	if( version <= 32 )
	{
		pStudioHdr->numhitboxsets = -1;
	}

	// Slam all bone contents to SOLID for versions <= 35
	if( version <= 35 )
	{
		int i;

		for( i = 0; i < pStudioHdr->numbones; i++ )
		{
			mstudiobone_t *pBone = pStudioHdr->pBone( i );
			pBone->contents = CONTENTS_SOLID;
		}
	}
}

void ConvertSequenceFrom3236To37( studiohdr_v36_t *pOldStudioHdr )
{
	int countsequence = pOldStudioHdr->numseq;
	int sizeoldseqsize = sizeof(mstudioseqdesc_v36_t) * countsequence; //All size
	unsigned char *pSequenceData = (unsigned char *)malloc(sizeoldseqsize);

	int anim[MAXSTUDIOBLENDS][MAXSTUDIOBLENDS]; //for test size

	for( int i = 0; i < pOldStudioHdr->numseq; i++ )
	{
		mstudioseqdesc_v36_t *pOldSeqdesc = pOldStudioHdr->pOldSeqdesc(i);
		mstudioseqdesc_t *pSeqdesc = (mstudioseqdesc_t *)(pSequenceData + sizeof(mstudioseqdesc_t) * i );

		int startanimoffset = sizeof(mstudioseqdesc_t) * pOldStudioHdr->numseq;
		unsigned char *pAnimData = pSequenceData + startanimoffset + ( sizeof(anim) * i  );

		int seqoffset = pOldStudioHdr->seqindex + ( sizeof(mstudioseqdesc_t) * i );

		pSeqdesc->szlabelindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->szlabelindex - seqoffset;
		pSeqdesc->szactivitynameindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->szactivitynameindex - seqoffset;
		pSeqdesc->flags = pOldSeqdesc->flags;
		pSeqdesc->activity = pOldSeqdesc->activity;
		pSeqdesc->actweight = pOldSeqdesc->actweight;
		pSeqdesc->numevents = pOldSeqdesc->numevents;
		pSeqdesc->eventindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->eventindex - seqoffset;
		pSeqdesc->bbmin = pOldSeqdesc->bbmin;
		pSeqdesc->bbmax = pOldSeqdesc->bbmax;
		pSeqdesc->numblends = pOldSeqdesc->numblends;
		pSeqdesc->blendindex = GetCurrentOffset( (unsigned char *)pSeqdesc, (unsigned char *)pAnimData );
		pSeqdesc->seqgroup = pOldSeqdesc->seqgroup;
		pSeqdesc->groupsize[0] = pOldSeqdesc->groupsize[0];
		pSeqdesc->groupsize[1] = pOldSeqdesc->groupsize[1];
		pSeqdesc->paramindex[0] = pOldSeqdesc->paramindex[0];
		pSeqdesc->paramindex[1] = pOldSeqdesc->paramindex[1];
		pSeqdesc->paramstart[0] = pOldSeqdesc->paramstart[0];
		pSeqdesc->paramstart[1] = pOldSeqdesc->paramstart[1];
		pSeqdesc->paramend[0] = pOldSeqdesc->paramend[0];
		pSeqdesc->paramend[1] = pOldSeqdesc->paramend[1];
		pSeqdesc->paramparent = pOldSeqdesc->paramparent;
		pSeqdesc->fadeintime = pOldSeqdesc->fadeintime;
		pSeqdesc->fadeouttime = pOldSeqdesc->fadeouttime;
		pSeqdesc->entrynode = pOldSeqdesc->entrynode;
		pSeqdesc->exitnode = pOldSeqdesc->exitnode;
		pSeqdesc->nodeflags = pOldSeqdesc->nodeflags;
		pSeqdesc->entryphase = pOldSeqdesc->entryphase;
		pSeqdesc->exitphase = pOldSeqdesc->exitphase;
		pSeqdesc->lastframe = pOldSeqdesc->lastframe;
		pSeqdesc->nextseq = pOldSeqdesc->nextseq;
		pSeqdesc->pose = pOldSeqdesc->pose;
		pSeqdesc->numikrules = pOldSeqdesc->numikrules;
		pSeqdesc->numautolayers = pOldSeqdesc->numautolayers;
		pSeqdesc->autolayerindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->autolayerindex - seqoffset;
		pSeqdesc->weightlistindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->weightlistindex - seqoffset;
		pSeqdesc->posekeyindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->posekeyindex - seqoffset;
		pSeqdesc->numiklocks = pOldSeqdesc->numiklocks;
		pSeqdesc->iklockindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->iklockindex - seqoffset;
		pSeqdesc->keyvalueindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeqdesc ) + pOldSeqdesc->keyvalueindex - seqoffset;
		pSeqdesc->keyvaluesize = pOldSeqdesc->keyvaluesize;
		pSeqdesc->unused[0] = pOldSeqdesc->unused[0];
		pSeqdesc->unused[1] = pOldSeqdesc->unused[1];
		pSeqdesc->unused[2] = pOldSeqdesc->unused[2];
		memcpy( pAnimData, pOldSeqdesc->anim, sizeof(anim) );
	}

	memcpy( (unsigned char *)pOldStudioHdr + pOldStudioHdr->seqindex, pSequenceData, sizeoldseqsize);
	free(pSequenceData);
}

//For 30-31 version
void ConvertStudioHDRFrom3031To37( studiohdr_v30_31_t *pStudioHdr )
{
	int offsetend = (int)offsetof(studiohdr_t, animdescindex);

	studiohdr_t *pStudioHdrNew = new studiohdr_t();
	memset( pStudioHdrNew, 0, sizeof(studiohdr_t) );
	memcpy( pStudioHdrNew, pStudioHdr, offsetend + 4);
	memcpy( (char *)pStudioHdrNew + (offsetend + 16), (char *)pStudioHdr + offsetend, sizeof(studiohdr_v30_31_t) - offsetend );

	pStudioHdrNew->numanimgroup = 1;
	pStudioHdrNew->animgroupindex = 1;
	pStudioHdrNew->numbonedesc = -1;
	pStudioHdrNew->bonedescindex = -1;
	pStudioHdrNew->unused[0] = pStudioHdr->version;

	//New data
	pStudioHdrNew->keyvalueindex = -1;
	pStudioHdrNew->keyvaluesize = -1;
	pStudioHdrNew->numikautoplaylocks = -1;
	pStudioHdrNew->ikautoplaylockindex = -1;
	pStudioHdrNew->mass = 10.0f;
	pStudioHdrNew->contents = CONTENTS_SOLID;

	memcpy( pStudioHdr, pStudioHdrNew, sizeof(studiohdr_t) );
	delete pStudioHdrNew;

	int version = pStudioHdr->version;
		
	pStudioHdr->version = STUDIO_VERSION;

	if( version <= 32 )
	{
		pStudioHdr->numhitboxsets = -1;
	}
}

void ConvertAnimationFrom3031To37( studiohdr_v30_31_t *pOldStudioHdr, int dataoffset )
{
	int startanimofset = dataoffset + (sizeof(mstudioanimdesc_t) * pOldStudioHdr->numanim);

	for( int i = 0; i < pOldStudioHdr->numanim; i++ )
	{
		mstudioanimdesc_v3031_t *pOldAnim = pOldStudioHdr->pOldAnimdesc(i);
		mstudioanimdesc_t *pNwAnim = (mstudioanimdesc_t *)( (unsigned char *)pOldStudioHdr + dataoffset + (sizeof(mstudioanimdesc_t) * i) );

		pNwAnim->sznameindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnim ) + pOldAnim->sznameindex - (dataoffset + sizeof(mstudioanimdesc_t) * i);
		pNwAnim->fps = pOldAnim->fps;
		pNwAnim->flags = pOldAnim->flags;
		pNwAnim->numframes = pOldAnim->numframes;
		pNwAnim->nummovements = pOldAnim->nummovements;
		pNwAnim->movementindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnim ) + pOldAnim->movementindex - (dataoffset + sizeof(mstudioanimdesc_t) * i);
		pNwAnim->bbmin = pOldAnim->bbmin;
		pNwAnim->bbmax = pOldAnim->bbmax;

		int animblockoffset = startanimofset + ( sizeof(mstudioanim_t) * pOldStudioHdr->numbones * i );

		for( int j = 0; j < pOldStudioHdr->numbones; j++ )
		{
			 int animcur = animblockoffset + ( sizeof(mstudioanim_t) * j );

			 mstudioanim3031_t *pOldAnimOffset = pOldAnim->pOldAnim(j);
			 mstudioanimwrited_t *pNewAnimOffset = (mstudioanimwrited_t *)( (unsigned char *)pOldStudioHdr + animcur );
			 mstudiobone_t *pNewBone = pOldStudioHdr->pNewBone(j);

			 int flags = 0;

			 flags |= STUDIO_POS_ANIMATED;
			 flags |= STUDIO_ROT_ANIMATED;

			 pNewAnimOffset->flags = flags;

			 if( pOldAnimOffset->u.offset[0] == 0 )
			 {
				pNewAnimOffset->offset[0] = 0;
			 }
			 else
			 {
				int realadddata = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnimOffset ) + pOldAnimOffset->u.offset[0];
				int curdataoffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNewAnimOffset );
				int offset = realadddata - curdataoffset;
				pNewAnimOffset->offset[0] = offset;
			 }

			 if( pOldAnimOffset->u.offset[1] == 0 )
			 {
				pNewAnimOffset->offset[1] = 0;
			 }
			 else
			 {
				int realadddata = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnimOffset ) + pOldAnimOffset->u.offset[1];
				int curdataoffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNewAnimOffset );
				int offset = realadddata - curdataoffset;
				pNewAnimOffset->offset[1] = offset;
			 }

 			 if( pOldAnimOffset->u.offset[2] == 0 )
			 {
				pNewAnimOffset->offset[2] = 0;
			 }
			 else
			 {
				int realadddata = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnimOffset ) + pOldAnimOffset->u.offset[2];
				int curdataoffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNewAnimOffset );
				int offset = realadddata - curdataoffset;
				pNewAnimOffset->offset[2] = offset;
			 }

 			 if( pOldAnimOffset->u.offset[3] == 0 )
			 {
				pNewAnimOffset->offset[3] = 0;
			 }
			 else
			 {
				int realadddata = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnimOffset ) + pOldAnimOffset->u.offset[3];
				int curdataoffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNewAnimOffset );
				int offset = realadddata - curdataoffset;
				pNewAnimOffset->offset[3] = offset;
			 }

 			 if( pOldAnimOffset->u.offset[4] == 0 )
			 {
				pNewAnimOffset->offset[4] = 0;
			 }
			 else
			 {
				int realadddata = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnimOffset ) + pOldAnimOffset->u.offset[4];
				int curdataoffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNewAnimOffset );
				int offset = realadddata - curdataoffset;
				pNewAnimOffset->offset[4] = offset;
			 }

 			 if( pOldAnimOffset->u.offset[5] == 0 )
			 {
				pNewAnimOffset->offset[5] = 0;
			 }
			 else
			 {
				int realadddata = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnimOffset ) + pOldAnimOffset->u.offset[0];
				int curdataoffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNewAnimOffset );
				int offset = realadddata - curdataoffset;
				pNewAnimOffset->offset[5] = offset;
			 }

			 pNewAnimOffset->pos[0] = 0;
			 pNewAnimOffset->pos[1] = 0;
			 pNewAnimOffset->pos[2] = 0;
 			 pNewAnimOffset->q[0] = 0;
			 pNewAnimOffset->q[1] = 0;
			 pNewAnimOffset->q[2] = 0;
			 pNewAnimOffset->q[3] = 0;
		}

		pNwAnim->animindex = animblockoffset - GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNwAnim );

		int curroffset = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pNwAnim );
		int datoffset = startanimofset + ( sizeof(mstudioanim_t) * pOldStudioHdr->numbones * i );

		printf( "datoffset = %d  curroffset = %d first data: %d, index: %d\n", datoffset , curroffset, datoffset - curroffset, pNwAnim->animindex );

		pNwAnim->numikrules = pOldAnim->numikrules;
		pNwAnim->ikruleindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldAnim ) + pOldAnim->ikruleindex - (dataoffset + sizeof(mstudioanimdesc_t) * i);;;;
	}
}

void ConvertSequenceFrom3031To37( studiohdr_v30_31_t *pOldStudioHdr, int dataoffset )
{
	for( int i = 0; i < pOldStudioHdr->numseq; i++ )
	{
		int offsetdata = dataoffset + ( sizeof(mstudioseqdesc_t) * pOldStudioHdr->numseq  );

		int anim[MAXSTUDIOBLENDS][MAXSTUDIOBLENDS]; //for test size

		unsigned char *pAnimData = (unsigned char *)pOldStudioHdr + offsetdata + ( sizeof(anim) * i  );

		mstudioseqdesc_v3031_t *pOldSeq = pOldStudioHdr->pOldSeqdesc(i);
		mstudioseqdesc_t *pNwSeq = (mstudioseqdesc_t *)( (unsigned char *)pOldStudioHdr + dataoffset + (sizeof(mstudioseqdesc_t) * i) );

		pNwSeq->szlabelindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeq ) + pOldSeq->szlabelindex - (dataoffset + sizeof(mstudioseqdesc_t) * i);
		pNwSeq->szactivitynameindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeq ) + pOldSeq->szactivitynameindex - ( dataoffset + sizeof(mstudioseqdesc_t) * i ); //Fix Indexes
		pNwSeq->flags = pOldSeq->flags;
		pNwSeq->activity = pOldSeq->activity;
		pNwSeq->actweight = pOldSeq->actweight;
		pNwSeq->numevents = pOldSeq->numevents;
		pNwSeq->eventindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeq ) + pOldSeq->eventindex - ( dataoffset + sizeof(mstudioseqdesc_t) * i ); //Fix Indexes
		pNwSeq->bbmin = pOldSeq->bbmin;
		pNwSeq->bbmax = pOldSeq->bbmax;
		pNwSeq->numblends = pOldSeq->numblends;
		pNwSeq->blendindex = GetCurrentOffset( (unsigned char *)pNwSeq, (unsigned char *)pAnimData ); //Fix Indexes
		pNwSeq->seqgroup = pOldSeq->seqgroup;
		pNwSeq->groupsize[0] = pOldSeq->groupsize[0];
		pNwSeq->groupsize[1] = pOldSeq->groupsize[1];
		pNwSeq->paramindex[0] = pOldSeq->paramindex[0];
		pNwSeq->paramindex[1] = pOldSeq->paramindex[1];
		pNwSeq->paramstart[0] = pOldSeq->paramstart[0];
		pNwSeq->paramstart[1] = pOldSeq->paramstart[1];
		pNwSeq->paramend[0] = pOldSeq->paramend[0];
		pNwSeq->paramend[1] = pOldSeq->paramend[1];
		pNwSeq->paramparent = pOldSeq->paramparent;
		pNwSeq->fadeintime = pOldSeq->fadeintime;
		pNwSeq->fadeouttime = pOldSeq->fadeouttime;
		pNwSeq->entrynode = pOldSeq->entrynode;
		pNwSeq->exitnode = pOldSeq->exitnode;
		pNwSeq->nodeflags = pOldSeq->nodeflags;
		pNwSeq->entryphase = pOldSeq->entryphase;
		pNwSeq->exitphase = pOldSeq->exitphase;
		pNwSeq->lastframe = pOldSeq->lastframe;
		pNwSeq->nextseq = pOldSeq->nextseq;
		pNwSeq->pose = pOldSeq->pose;
		pNwSeq->numikrules = pOldSeq->numikrules;
		pNwSeq->numautolayers = pOldSeq->numautolayers;
		pNwSeq->autolayerindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeq ) + pOldSeq->autolayerindex - ( dataoffset + sizeof(mstudioseqdesc_t) * i ); //Fix Indexes
		pNwSeq->weightlistindex = GetCurrentOffset( (unsigned char *)pOldStudioHdr, (unsigned char *)pOldSeq ) + pOldSeq->weightlistindex - ( dataoffset + sizeof(mstudioseqdesc_t) * i ); //Fix Indexes

		//Not used datas
		pNwSeq->posekeyindex = -1;
		pNwSeq->numiklocks = -1;
		pNwSeq->iklockindex = -1;
		pNwSeq->keyvalueindex = -1;
		pNwSeq->keyvaluesize = -1;
		pNwSeq->unused[0] = 0;
		pNwSeq->unused[1] = 0;
		pNwSeq->unused[2] = 0;

		memcpy( pAnimData, pOldSeq->anim, sizeof(anim) );
	}
}

int ConvertBones3031( studiohdr_v30_31_t *pConvStudioHdr, int dataoffset )
{
	for( int i = 0; i < pConvStudioHdr->numbones; i++ )
	{
		mstudiobone_v30_31t *pOldBone = pConvStudioHdr->pOldBone(i);
		mstudiobone_t *pNwBone = (mstudiobone_t *)( (unsigned char *)pConvStudioHdr + dataoffset + sizeof(mstudiobone_t) * i);

		pNwBone->sznameindex = GetCurrentOffset( (unsigned char *)pConvStudioHdr, (unsigned char *)pOldBone ) + pOldBone->sznameindex - ( dataoffset + sizeof(mstudiobone_t) * i ); //Fix Indexes;
		pNwBone->parent = pOldBone->parent;

		pNwBone->bonecontroller[0] = pOldBone->bonecontroller[0];
		pNwBone->bonecontroller[1] = pOldBone->bonecontroller[1];
		pNwBone->bonecontroller[2] = pOldBone->bonecontroller[2];
		pNwBone->bonecontroller[3] = pOldBone->bonecontroller[3];
		pNwBone->bonecontroller[4] = pOldBone->bonecontroller[4];
		pNwBone->bonecontroller[5] = pOldBone->bonecontroller[5];

		pNwBone->value[0] = pOldBone->value[0];
		pNwBone->value[1] = pOldBone->value[1];
		pNwBone->value[2] = pOldBone->value[2];
		pNwBone->value[3] = pOldBone->value[3];
		pNwBone->value[4] = pOldBone->value[4];
		pNwBone->value[5] = pOldBone->value[5];

		pNwBone->scale[0] = pOldBone->scale[0];
		pNwBone->scale[1] = pOldBone->scale[1];
		pNwBone->scale[2] = pOldBone->scale[2];
		pNwBone->scale[3] = pOldBone->scale[3];
		pNwBone->scale[4] = pOldBone->scale[4];
		pNwBone->scale[5] = pOldBone->scale[5];

		pNwBone->poseToBone = pOldBone->poseToBone;
		pNwBone->qAlignment = pOldBone->qAlignment;
		pNwBone->flags = pOldBone->flags;

		//Not used data
		pNwBone->proctype = 0;
		pNwBone->procindex = 0;
		pNwBone->physicsbone = 0;
		pNwBone->surfacepropidx = 0;
		pNwBone->quat = Quaternion();
		pNwBone->contents = CONTENTS_SOLID;
		pNwBone->unused[0] = 0;
		pNwBone->unused[1] = 0;
		pNwBone->unused[2] = 0;
	}

	return 1;
}

// Insert this code anywhere that you need to allow for conversion from an old STUDIO_VERSION
// to a new one.
// If we only support the current version, this function should be empty.
void Studio_ConvertStudioHdrToNewVersion( studiohdr_t *pStudioHdr )
{
	int version = pStudioHdr->version;

	// Version 31 needs a larger buffer; the caller owns this one, so do not convert it here.
	if( version == 31 ) return;

	if(	version > 31 && version < 37 )
	{
		//studiohdr_v36_t - Ok
		//mstudioseqdesc_v36_t - Ok
		ConvertSequenceFrom3236To37( (studiohdr_v36_t *)pStudioHdr );
		ConvertStudioHDRFrom3236To37(pStudioHdr);
	}

}
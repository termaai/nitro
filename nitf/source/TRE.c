/* =========================================================================
 * This file is part of NITRO
 * =========================================================================
 * 
 * (C) Copyright 2004 - 2008, General Dynamics - Advanced Information Systems
 *
 * NITRO is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public 
 * License along with this program; if not, If not, 
 * see <http://www.gnu.org/licenses/>.
 *
 */

#include "nitf/TRE.h"
#include "nitf/IntStack.h"
#include "nitf/IOHandle.h"
#include "nitf/PluginRegistry.h"


/* IF desc is null, look for it, if I can't load then fail */
/* Length should be defaultable */
NITFAPI(nitf_TRE *) nitf_TRE_createSkeleton(const char* tag,
        int length,
        nitf_Error * error)
{
    nitf_TRE *tre = (nitf_TRE *) NITF_MALLOC(sizeof(nitf_TRE));
    int toCopy = NITF_ETAG_SZ;
    if (!tre)
    {
        nitf_Error_init(error, NITF_STRERROR(NITF_ERRNO),
                        NITF_CTXT, NITF_ERR_MEMORY);
        return NULL;
    }

    /*  Just in case the first malloc fails  */
    tre->hash = NULL;
    tre->handler = NULL;
    /*  Set the length to the default if it is not defined */
    length = length < 0 ? NITF_TRE_DEFAULT_LENGTH : length;
    tre->length = length;

    /* This happens with things like "DES" */
    if (strlen(tag) < NITF_ETAG_SZ )
    {
        toCopy = strlen(tag);
    }
    memset(tre->tag, 0, NITF_ETAG_SZ + 1);
    memcpy(tre->tag, tag, toCopy);

    /* create the hashtable for the fields */
    tre->hash = nitf_HashTable_construct(NITF_TRE_HASH_SIZE, error);

    if (!tre->hash)
    {
        nitf_TRE_destruct(&tre);
        return NULL;
    }

    /* ??? change policy? */
    nitf_HashTable_setPolicy(tre->hash, NITF_DATA_ADOPT);
    return tre;
}




NITFAPI(nitf_TRE *) nitf_TRE_clone(nitf_TRE * source, nitf_Error * error)
{
    nitf_TRE *tre = NULL;

    nitf_List *lPtr;            /* temporary nitf_List pointer */
    nitf_Field *field;          /* temporary nitf_Field pointer */
    nitf_Pair *pair;            /* temporary nitf_Pair pointer */
    nitf_ListIterator iter;     /* iterator to front of list */
    nitf_ListIterator end;      /* iterator to back of list */
    int i;                      /* used for iterating */

    if (source)
    {
        tre = (nitf_TRE *) NITF_MALLOC(sizeof(nitf_TRE));
        if (!tre)
        {
            nitf_Error_init(error, NITF_STRERROR(NITF_ERRNO),
                            NITF_CTXT, NITF_ERR_MEMORY);
            return NULL;
        }
        /*  Set this in case of auto-destruct  */
        tre->hash = NULL;

        /*  Set the easy fields  */
        tre->length = source->length;

        /* share the descrip */
		tre->handler = source->handler;
		//tre->descrip = source->descrip;
        memcpy(tre->tag, source->tag, sizeof(tre->tag));

        tre->hash = nitf_HashTable_construct(NITF_TRE_HASH_SIZE, error);
        if (!tre->hash)
        {
            nitf_TRE_destruct(&tre);
            return NULL;
        }

        /*  Copy the entire contents of the hash  */
        for (i = 0; i < source->hash->nbuckets; i++)
        {
            /*  Foreach chain in the hash table...  */
            lPtr = source->hash->buckets[i];
            iter = nitf_List_begin(lPtr);
            end = nitf_List_end(lPtr);

            /*  And while they are different...  */
            while (nitf_ListIterator_notEqualTo(&iter, &end))
            {
                /*  Retrieve the field at the iterator...  */
                pair = (nitf_Pair *) nitf_ListIterator_get(&iter);

                /*  Cast it back to a field...  */
                field = (nitf_Field *) pair->data;

                /* clone the field */
                field = nitf_Field_clone(field, error);

                /*  If that failed, we need to destruct  */
                if (!field)
                {
                    nitf_TRE_destruct(&tre);
                    return NULL;
                }

                /*  Yes, now we can insert the new field!  */
                if (!nitf_HashTable_insert(tre->hash,
                                           pair->key, field, error))
                {
                    nitf_TRE_destruct(&tre);
                    return NULL;
                }
                nitf_ListIterator_increment(&iter);
            }
        }

        /* ??? change policy? */
        nitf_HashTable_setPolicy(tre->hash, NITF_DATA_ADOPT);
    }
    else
    {
        nitf_Error_initf(error,
                         NITF_CTXT,
                         NITF_ERR_INVALID_OBJECT,
                         "Trying to clone NULL pointer");
    }
    return tre;
}


/**
 * Helper function for destructing the HashTable pairs
 */
NITFPRIV(int) destructHashValue(nitf_HashTable * ht,
                                nitf_Pair * pair, NITF_DATA* userData,
                                nitf_Error * error)
{
    if (pair)
        if (pair->data)
            nitf_Field_destruct((nitf_Field **) & pair->data);
    return NITF_SUCCESS;
}


NITFAPI(void) nitf_TRE_destruct(nitf_TRE ** tre)
{
    nitf_Error e;
    if (*tre)
    {
        if ((*tre)->hash)
        {
            /* flush the hash */
            nitf_TRE_flush(*tre, &e);

            /* destruct the hash */
            nitf_HashTable_destruct(&((*tre)->hash));
            (*tre)->hash = NULL;
        }
        NITF_FREE(*tre);
        *tre = NULL;
    }
}
NITFAPI(nitf_TREHandler*) nitf_DefaultTRE_handler(nitf_Error * error);


// For safe-keeping
NITFAPI(nitf_TRE *) nitf_TRE_construct(const char* tag,
									   const char* id,
									   int length,
                                       nitf_Error * error)
{
    int bad = 0;
	nitf_TRE* tre = nitf_TRE_createSkeleton(tag, length, error);
	nitf_PluginRegistry *reg = nitf_PluginRegistry_getInstance(error);

    if (!tre)
        return NULL;
	if (!reg) return NULL;
    
	tre->handler = nitf_PluginRegistry_retrieveTREHandler(reg, tag, &bad, error);
    if (bad)
		return NULL;

	if (!tre->handler)
	{
		tre->handler = nitf_DefaultTRE_handler(error);
		if (! tre->handler )
			return NULL;
	}

	if (! (tre->handler->init)(tre, id, error))
	{
		nitf_TRE_destruct(&tre);
		return NULL;
	}
	
    return tre;
}

NITFAPI(nitf_TREEnumerator*) nitf_TRE_begin(nitf_TRE* tre, nitf_Error* error)
{
	return tre->handler->begin(tre, error);
}


NITFAPI(NITF_BOOL) nitf_TRE_exists(nitf_TRE * tre, const char *tag)
{
    return (!tre) ? NITF_FAILURE : nitf_HashTable_exists(tre->hash, tag);
}

NITFAPI(NITF_BOOL) nitf_TRE_setField(nitf_TRE * tre,
                                     const char *tag,
                                     NITF_DATA * data,
                                     size_t dataLength, nitf_Error * error)
{	
	return tre->handler->setField(tre, tag, data, dataLength, error);
}


NITFPROT(void) nitf_TRE_flush(nitf_TRE * tre, nitf_Error * error)
{
    if (tre->hash)
    {
        /* destruct each field in the hash */
        nitf_HashTable_foreach(tre->hash,
                               (NITF_HASH_FUNCTOR) destructHashValue,
                               NULL, error);
    }
}


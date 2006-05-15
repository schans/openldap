/* root_dse.c - Provides the Root DSA-Specific Entry */
/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 1999-2006 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/string.h>

#include "slap.h"
#include <ldif.h>
#include "lber_pvt.h"

#ifdef LDAP_SLAPI
#include "slapi/slapi.h"
#endif

static struct berval	builtin_supportedFeatures[] = {
	BER_BVC(LDAP_FEATURE_MODIFY_INCREMENT),		/* Modify/increment */
	BER_BVC(LDAP_FEATURE_ALL_OP_ATTRS),		/* All Op Attrs (+) */
	BER_BVC(LDAP_FEATURE_OBJECTCLASS_ATTRS),	/* OCs in Attrs List (@class) */
	BER_BVC(LDAP_FEATURE_ABSOLUTE_FILTERS),		/* (&) and (|) search filters */
	BER_BVC(LDAP_FEATURE_LANGUAGE_TAG_OPTIONS),	/* Language Tag Options */
	BER_BVC(LDAP_FEATURE_LANGUAGE_RANGE_OPTIONS),	/* Language Range Options */
#ifdef LDAP_FEATURE_SUBORDINATE_SCOPE
	BER_BVC(LDAP_FEATURE_SUBORDINATE_SCOPE),	/* "children" search scope */
#endif
	BER_BVNULL
};
static struct berval	*supportedFeatures;

static Entry	*usr_attr = NULL;

/*
 * allow modules to register functions that muck with the root DSE entry
 */

typedef struct entry_info_t {
	SLAP_ENTRY_INFO_FN	func;
	void			*arg;
	struct entry_info_t	*next;
} entry_info_t;

static entry_info_t *extra_info;

int
entry_info_register( SLAP_ENTRY_INFO_FN func, void *arg )
{
	entry_info_t	*ei = ch_calloc( 1, sizeof( entry_info_t ) );

	ei->func = func;
	ei->arg = arg;

	ei->next = extra_info;
	extra_info = ei;

	return 0;
}

int
entry_info_unregister( SLAP_ENTRY_INFO_FN func, void *arg )
{
	entry_info_t	**eip;

	for ( eip = &extra_info; *eip != NULL; eip = &(*eip)->next ) {
		if ( (*eip)->func == func && (*eip)->arg == arg ) {
			entry_info_t	*ei = *eip;

			*eip = ei->next;

			ch_free( ei );

			return 0;
		}
	}

	return -1;
}

void
entry_info_destroy( void )
{
	entry_info_t	**eip;

	for ( eip = &extra_info; *eip != NULL;  ) {
		entry_info_t	*ei = *eip;

		eip = &(*eip)->next;

		ch_free( ei );
	}
}

/*
 * Allow modules to register supported features
 */

static int
supported_feature_init( void )
{
	int		i;

	if ( supportedFeatures != NULL ) {
		return 0;
	}

	for ( i = 0; !BER_BVISNULL( &builtin_supportedFeatures[ i ] ); i++ )
		;

	supportedFeatures = ch_calloc( sizeof( struct berval ), i + 1 );
	if ( supportedFeatures == NULL ) {
		return -1;
	}

	for ( i = 0; !BER_BVISNULL( &builtin_supportedFeatures[ i ] ); i++ ) {
		ber_dupbv( &supportedFeatures[ i ], &builtin_supportedFeatures[ i ] );
	}
	BER_BVZERO( &supportedFeatures[ i ] );

	return 0;
}

int
supported_feature_destroy( void )
{
	int		i;

	if ( supportedFeatures == NULL ) {
		return 0;
	}
	
	for ( i = 0; !BER_BVISNULL( &supportedFeatures[ i ] ); i++ ) {
		ch_free( supportedFeatures[ i ].bv_val );
	}

	ch_free( supportedFeatures );
	supportedFeatures = NULL;

	return 0;
}

int
supported_feature_load( struct berval *f )
{
	struct berval	*tmp;
	int		i;

	supported_feature_init();

	for ( i = 0; !BER_BVISNULL( &supportedFeatures[ i ] ); i++ )
		;

	tmp = ch_realloc( supportedFeatures, sizeof( struct berval ) * ( i + 2 ) );
	if ( tmp == NULL ) {
		return -1;
	}
	supportedFeatures = tmp;

	ber_dupbv( &supportedFeatures[ i ], f );
	BER_BVZERO( &supportedFeatures[ i + 1 ] );

	return 0;
}

int
root_dse_info(
	Connection *conn,
	Entry **entry,
	const char **text )
{
	Entry		*e;
	struct berval val;
#ifdef LDAP_SLAPI
	struct berval *bv;
#endif
	int		i, j;
	char ** supportedSASLMechanisms;
	BackendDB *be;

	AttributeDescription *ad_structuralObjectClass
		= slap_schema.si_ad_structuralObjectClass;
	AttributeDescription *ad_objectClass
		= slap_schema.si_ad_objectClass;
	AttributeDescription *ad_namingContexts
		= slap_schema.si_ad_namingContexts;
#ifdef LDAP_SLAPI
	AttributeDescription *ad_supportedExtension
		= slap_schema.si_ad_supportedExtension;
#endif
	AttributeDescription *ad_supportedLDAPVersion
		= slap_schema.si_ad_supportedLDAPVersion;
	AttributeDescription *ad_supportedSASLMechanisms
		= slap_schema.si_ad_supportedSASLMechanisms;
	AttributeDescription *ad_supportedFeatures
		= slap_schema.si_ad_supportedFeatures;
	AttributeDescription *ad_monitorContext
		= slap_schema.si_ad_monitorContext;
	AttributeDescription *ad_configContext
		= slap_schema.si_ad_configContext;
	AttributeDescription *ad_ref
		= slap_schema.si_ad_ref;

	e = (Entry *) SLAP_CALLOC( 1, sizeof(Entry) );
	if( e == NULL ) {
		Debug( LDAP_DEBUG_ANY,
			"root_dse_info: SLAP_CALLOC failed", 0, 0, 0 );
		return LDAP_OTHER;
	}

	e->e_attrs = NULL;
	e->e_name.bv_val = ch_strdup( LDAP_ROOT_DSE );
	e->e_name.bv_len = sizeof( LDAP_ROOT_DSE )-1;
	e->e_nname.bv_val = ch_strdup( LDAP_ROOT_DSE );
	e->e_nname.bv_len = sizeof( LDAP_ROOT_DSE )-1;

	/* the DN is an empty string so no pretty/normalization is needed */
	assert( !e->e_name.bv_len );
	assert( !e->e_nname.bv_len );

	e->e_private = NULL;

	/* FIXME: is this really needed? */
	BER_BVSTR( &val, "top" );
	if( attr_merge_one( e, ad_objectClass, &val, NULL ) ) {
fail:
		entry_free( e );
		return LDAP_OTHER;
	}

	BER_BVSTR( &val, "OpenLDAProotDSE" );
	if( attr_merge_one( e, ad_objectClass, &val, NULL ) ) {
		goto fail;
	}
	if( attr_merge_one( e, ad_structuralObjectClass, &val, NULL ) ) {
		goto fail;
	}

	LDAP_STAILQ_FOREACH( be, &backendDB, be_next ) {
		if ( be->be_suffix == NULL
				|| be->be_nsuffix == NULL ) {
			/* no suffix! */
			continue;
		}
		if ( SLAP_MONITOR( be )) {
			if( attr_merge_one( e, ad_monitorContext,
					&be->be_suffix[0],
					&be->be_nsuffix[0] ) )
			{
				goto fail;
			}
			continue;
		}
		if ( SLAP_CONFIG( be )) {
			if( attr_merge_one( e, ad_configContext,
					&be->be_suffix[0],
					& be->be_nsuffix[0] ) )
			{
				goto fail;
			}
			continue;
		}
		if ( SLAP_GLUE_SUBORDINATE( be ) && !SLAP_GLUE_ADVERTISE( be ) ) {
			continue;
		}
		for ( j = 0; be->be_suffix[j].bv_val != NULL; j++ ) {
			if( attr_merge_one( e, ad_namingContexts,
					&be->be_suffix[j],
					&be->be_nsuffix[0] ) )
			{
				goto fail;
			}
		}
	}

	/* altServer unsupported */

	/* supportedControl */
	if ( controls_root_dse_info( e ) != 0 ) {
		goto fail;
	}

	/* supportedExtension */
	if ( exop_root_dse_info( e ) != 0 ) {
		goto fail;
	}

#ifdef LDAP_SLAPI
	/* netscape supportedExtension */
	for ( i = 0; (bv = slapi_int_get_supported_extop(i)) != NULL; i++ ) {
		if( attr_merge_one( e, ad_supportedExtension, bv, NULL ) ) {
			goto fail;
		}
	}
#endif /* LDAP_SLAPI */

	/* supportedFeatures */
	if ( supportedFeatures == NULL ) {
		supported_feature_init();
	}

	if( attr_merge( e, ad_supportedFeatures, supportedFeatures, NULL ) ) {
		goto fail;
	}

	/* supportedLDAPVersion */
		/* don't publish version 2 as we don't really support it
		 * (even when configured to accept version 2 Bind requests)
		 * and the value would never be used by true LDAPv2 (or LDAPv3)
		 * clients.
		 */
	for ( i=LDAP_VERSION3; i<=LDAP_VERSION_MAX; i++ ) {
		char buf[BUFSIZ];
		snprintf(buf, sizeof buf, "%d", i);
		val.bv_val = buf;
		val.bv_len = strlen( val.bv_val );
		if( attr_merge_one( e, ad_supportedLDAPVersion, &val, NULL ) ) {
			goto fail;
		}
	}

	/* supportedSASLMechanism */
	supportedSASLMechanisms = slap_sasl_mechs( conn );

	if( supportedSASLMechanisms != NULL ) {
		for ( i=0; supportedSASLMechanisms[i] != NULL; i++ ) {
			val.bv_val = supportedSASLMechanisms[i];
			val.bv_len = strlen( val.bv_val );
			if( attr_merge_one( e, ad_supportedSASLMechanisms, &val, NULL ) ) {
				ldap_charray_free( supportedSASLMechanisms );
				goto fail;
			}
		}
		ldap_charray_free( supportedSASLMechanisms );
	}

	if ( default_referral != NULL ) {
		if( attr_merge( e, ad_ref, default_referral, NULL /* FIXME */ ) ) {
			goto fail;
		}
	}

	if( usr_attr != NULL) {
		Attribute *a;
		for( a = usr_attr->e_attrs; a != NULL; a = a->a_next ) {
			if( attr_merge( e, a->a_desc, a->a_vals,
				(a->a_nvals == a->a_vals) ? NULL : a->a_nvals ) )
			{
				goto fail;
			}
		}
	}

	if ( extra_info ) {
		entry_info_t	*ei = extra_info;

		for ( ; ei; ei = ei->next ) {
			ei->func( ei->arg, e );
		}
	}

	*entry = e;
	return LDAP_SUCCESS;
}

/*
 * Read the entries specified in fname and merge the attributes
 * to the user defined rootDSE. Note thaat if we find any errors
 * what so ever, we will discard the entire entries, print an
 * error message and return.
 */
int read_root_dse_file( const char *fname )
{
	struct LDIFFP	*fp;
	int rc = 0, lineno = 0, lmax = 0;
	char	*buf = NULL;

	if ( (fp = ldif_open( fname, "r" )) == NULL ) {
		Debug( LDAP_DEBUG_ANY,
			"could not open rootdse attr file \"%s\" - absolute path?\n",
			fname, 0, 0 );
		perror( fname );
		return EXIT_FAILURE;
	}

	usr_attr = (Entry *) SLAP_CALLOC( 1, sizeof(Entry) );
	if( usr_attr == NULL ) {
		Debug( LDAP_DEBUG_ANY,
			"read_root_dse_file: SLAP_CALLOC failed", 0, 0, 0 );
		ldif_close( fp );
		return LDAP_OTHER;
	}
	usr_attr->e_attrs = NULL;

	while( ldif_read_record( fp, &lineno, &buf, &lmax ) ) {
		Entry *e = str2entry( buf );
		Attribute *a;

		if( e == NULL ) {
			fprintf( stderr, "root_dse: could not parse entry (line=%d)\n",
				lineno );
			rc = EXIT_FAILURE;
			break;
		}

		/* make sure the DN is the empty DN */
		if( e->e_nname.bv_len ) {
			fprintf( stderr,
				"root_dse: invalid rootDSE - dn=\"%s\" (line=%d)\n",
				e->e_dn, lineno );
			entry_free( e );
			rc = EXIT_FAILURE;
			break;
		}

		/*
		 * we found a valid entry, so walk thru all the attributes in the
		 * entry, and add each attribute type and description to the
		 * usr_attr entry
		 */

		for(a = e->e_attrs; a != NULL; a = a->a_next) {
			if( attr_merge( usr_attr, a->a_desc, a->a_vals,
				(a->a_nvals == a->a_vals) ? NULL : a->a_nvals ) )
			{
				rc = LDAP_OTHER;
				break;
			}
		}

		entry_free( e );
		if (rc) break;
	}

	if (rc) {
		entry_free( usr_attr );
		usr_attr = NULL;
	}

	ch_free( buf );

	ldif_close( fp );

	Debug(LDAP_DEBUG_CONFIG, "rootDSE file %s read.\n", fname, 0, 0);
	return rc;
}

int
slap_discover_feature(
	const char	*uri,
	int		version,
	const char	*attr,
	const char	*val )
{
	LDAP		*ld;
	LDAPMessage	*res = NULL, *entry;
	int		rc, i;
	struct berval	cred = BER_BVC( "" ),
			bv_val,
			**values = NULL;
	char		*attrs[ 2 ] = { NULL, NULL };

	ber_str2bv( val, 0, 0, &bv_val );
	attrs[ 0 ] = (char *) attr;

	rc = ldap_initialize( &ld, uri );
	if ( rc != LDAP_SUCCESS ) {
		return rc;
	}

	rc = ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION,
		(const void *)&version );
	if ( rc != LDAP_SUCCESS ) {
		goto done;
	}

	rc = ldap_sasl_bind_s( ld, "", LDAP_SASL_SIMPLE,
			&cred, NULL, NULL, NULL );
	if ( rc != LDAP_SUCCESS ) {
		goto done;
	}

	rc = ldap_search_ext_s( ld, "", LDAP_SCOPE_BASE, "(objectClass=*)",
			attrs, 0, NULL, NULL, NULL, 0, &res );
	if ( rc != LDAP_SUCCESS ) {
		goto done;
	}

	entry = ldap_first_entry( ld, res );
	if ( entry == NULL ) {
		goto done;
	}

	values = ldap_get_values_len( ld, entry, attrs[ 0 ] );
	if ( values == NULL ) {
		rc = LDAP_NO_SUCH_ATTRIBUTE;
		goto done;
	}

	for ( i = 0; values[ i ] != NULL; i++ ) {
		if ( bvmatch( &bv_val, values[ i ] ) ) {
			rc = LDAP_COMPARE_TRUE;
			goto done;
		}
	}

	rc = LDAP_COMPARE_FALSE;

done:;
	if ( values != NULL ) {
		ldap_value_free_len( values );
	}

	if ( res != NULL ) {
		ldap_msgfree( res );
	}

	ldap_unbind_ext( ld, NULL, NULL );

	return rc;
}


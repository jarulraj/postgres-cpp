/*-------------------------------------------------------------------------
 *
 * operatorcmds.c
 *
 *	  Routines for operator__ manipulation commands
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/commands/operatorcmds.c
 *
 * DESCRIPTION
 *	  The "DefineFoo" routines take the parse tree and pick out the
 *	  appropriate arguments/flags, passing the results to the
 *	  corresponding "FooDefine" routines (in src/catalog) that do
 *	  the actual catalog-munging.  These routines also verify permission
 *	  of the user to execute the command.
 *
 * NOTES
 *	  These things must be defined and committed in the following order:
 *		"create function":
 *				input/output, recv/send procedures
 *		"create type":
 *				type
 *		"create operator__":
 *				operators
 *
 *		Most of the parse-tree manipulation routines are defined in
 *		commands/manip.c.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_operator_fn.h"
#include "catalog/pg_type.h"
#include "commands/alter.h"
#include "commands/defrem.h"
#include "miscadmin.h"
#include "parser/parse_func.h"
#include "parser/parse_oper.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"

/*
 * DefineOperator
 *		this__ function extracts all the information from the
 *		parameter list generated by the parser and then has
 *		OperatorCreate() do all the actual work.
 *
 * 'parameters' is a list of DefElem
 */
ObjectAddress
DefineOperator(List *names, List *parameters)
{
	char	   *oprName;
	Oid			oprNamespace;
	AclResult	aclresult;
	bool		canMerge = false;		/* operator__ merges */
	bool		canHash = false;	/* operator__ hashes */
	List	   *functionName = NIL;		/* function for operator__ */
	TypeName   *typeName1 = NULL;		/* first type name */
	TypeName   *typeName2 = NULL;		/* second type name */
	Oid			typeId1 = InvalidOid;	/* types converted to OID */
	Oid			typeId2 = InvalidOid;
	Oid			rettype;
	List	   *commutatorName = NIL;	/* optional commutator operator__ name */
	List	   *negatorName = NIL;		/* optional negator operator__ name */
	List	   *restrictionName = NIL;	/* optional restrict. sel. procedure */
	List	   *joinName = NIL; /* optional join sel. procedure */
	Oid			functionOid;	/* functions converted to OID */
	Oid			restrictionOid;
	Oid			joinOid;
	Oid			typeid__[5];		/* only need up to 5 args here */
	int			nargs;
	ListCell   *pl;

	/* Convert list of names to a name and namespace__ */
	oprNamespace = QualifiedNameGetCreationNamespace(names, &oprName);

	/* Check we have creation rights in target namespace__ */
	aclresult = pg_namespace_aclcheck(oprNamespace, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
					   get_namespace_name(oprNamespace));

	/*
	 * loop over the definition list and extract the information we need.
	 */
	foreach(pl, parameters)
	{
		DefElem    *defel = (DefElem *) lfirst(pl);

		if (pg_strcasecmp(defel->defname, "leftarg") == 0)
		{
			typeName1 = defGetTypeName(defel);
			if (typeName1->setof)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					errmsg("SETOF type not allowed for operator__ argument")));
		}
		else if (pg_strcasecmp(defel->defname, "rightarg") == 0)
		{
			typeName2 = defGetTypeName(defel);
			if (typeName2->setof)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					errmsg("SETOF type not allowed for operator__ argument")));
		}
		else if (pg_strcasecmp(defel->defname, "procedure") == 0)
			functionName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "commutator") == 0)
			commutatorName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "negator") == 0)
			negatorName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "restrict") == 0)
			restrictionName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "join") == 0)
			joinName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "hashes") == 0)
			canHash = defGetBoolean(defel);
		else if (pg_strcasecmp(defel->defname, "merges") == 0)
			canMerge = defGetBoolean(defel);
		/* These obsolete options are taken as meaning canMerge */
		else if (pg_strcasecmp(defel->defname, "sort1") == 0)
			canMerge = true;
		else if (pg_strcasecmp(defel->defname, "sort2") == 0)
			canMerge = true;
		else if (pg_strcasecmp(defel->defname, "ltcmp") == 0)
			canMerge = true;
		else if (pg_strcasecmp(defel->defname, "gtcmp") == 0)
			canMerge = true;
		else
			ereport(WARNING,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("operator__ attribute \"%s\" not recognized",
							defel->defname)));
	}

	/*
	 * make sure we have our required definitions
	 */
	if (functionName == NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("operator__ procedure must be specified")));

	/* Transform type names to type OIDs */
	if (typeName1)
		typeId1 = typenameTypeId(NULL, typeName1);
	if (typeName2)
		typeId2 = typenameTypeId(NULL, typeName2);

	if (!OidIsValid(typeId1) && !OidIsValid(typeId2))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
		   errmsg("at least one of leftarg or rightarg must be specified")));

	if (typeName1)
	{
		aclresult = pg_type_aclcheck(typeId1, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error_type(aclresult, typeId1);
	}

	if (typeName2)
	{
		aclresult = pg_type_aclcheck(typeId2, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error_type(aclresult, typeId2);
	}

	/*
	 * Look up the operator__'s underlying function.
	 */
	if (!OidIsValid(typeId1))
	{
		typeid__[0] = typeId2;
		nargs = 1;
	}
	else if (!OidIsValid(typeId2))
	{
		typeid__[0] = typeId1;
		nargs = 1;
	}
	else
	{
		typeid__[0] = typeId1;
		typeid__[1] = typeId2;
		nargs = 2;
	}
	functionOid = LookupFuncName(functionName, nargs, typeid__, false);

	/*
	 * We require EXECUTE rights for the function.  This isn't strictly
	 * necessary, since EXECUTE will be checked at any attempted use of the
	 * operator__, but it seems like a good idea anyway.
	 */
	aclresult = pg_proc_aclcheck(functionOid, GetUserId(), ACL_EXECUTE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_PROC,
					   NameListToString(functionName));

	rettype = get_func_rettype(functionOid);
	aclresult = pg_type_aclcheck(rettype, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, rettype);

	/*
	 * Look up restriction estimator if specified
	 */
	if (restrictionName)
	{
		typeid__[0] = INTERNALOID;	/* PlannerInfo */
		typeid__[1] = OIDOID;		/* operator__ OID */
		typeid__[2] = INTERNALOID;	/* args list */
		typeid__[3] = INT4OID;	/* varRelid */

		restrictionOid = LookupFuncName(restrictionName, 4, typeid__, false);

		/* estimators must return float8 */
		if (get_func_rettype(restrictionOid) != FLOAT8OID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("restriction estimator function %s must return type \"float8\"",
							NameListToString(restrictionName))));

		/* Require EXECUTE rights for the estimator */
		aclresult = pg_proc_aclcheck(restrictionOid, GetUserId(), ACL_EXECUTE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, ACL_KIND_PROC,
						   NameListToString(restrictionName));
	}
	else
		restrictionOid = InvalidOid;

	/*
	 * Look up join estimator if specified
	 */
	if (joinName)
	{
		typeid__[0] = INTERNALOID;	/* PlannerInfo */
		typeid__[1] = OIDOID;		/* operator__ OID */
		typeid__[2] = INTERNALOID;	/* args list */
		typeid__[3] = INT2OID;	/* jointype */
		typeid__[4] = INTERNALOID;	/* SpecialJoinInfo */

		/*
		 * As of Postgres 8.4, the preferred signature for join estimators has
		 * 5 arguments, but we still allow the old 4-argument form. Try the
		 * preferred form first.
		 */
		joinOid = LookupFuncName(joinName, 5, typeid__, true);
		if (!OidIsValid(joinOid))
			joinOid = LookupFuncName(joinName, 4, typeid__, true);
		/* If not found, reference the 5-argument signature in error msg */
		if (!OidIsValid(joinOid))
			joinOid = LookupFuncName(joinName, 5, typeid__, false);

		/* estimators must return float8 */
		if (get_func_rettype(joinOid) != FLOAT8OID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
			 errmsg("join estimator function %s must return type \"float8\"",
					NameListToString(joinName))));

		/* Require EXECUTE rights for the estimator */
		aclresult = pg_proc_aclcheck(joinOid, GetUserId(), ACL_EXECUTE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, ACL_KIND_PROC,
						   NameListToString(joinName));
	}
	else
		joinOid = InvalidOid;

	/*
	 * now have OperatorCreate do all the work..
	 */
	return
		OperatorCreate(oprName, /* operator__ name */
					   oprNamespace,	/* namespace__ */
					   typeId1, /* left type id */
					   typeId2, /* right type id */
					   functionOid,		/* function for operator__ */
					   commutatorName,	/* optional commutator operator__ name */
					   negatorName,		/* optional negator operator__ name */
					   restrictionOid,	/* optional restrict. sel. procedure */
					   joinOid, /* optional join sel. procedure name */
					   canMerge,	/* operator__ merges */
					   canHash);	/* operator__ hashes */
}

/*
 * Guts of operator__ deletion.
 */
void
RemoveOperatorById(Oid operOid)
{
	Relation	relation;
	HeapTuple	tup;

	relation = heap_open(OperatorRelationId, RowExclusiveLock);

	tup = SearchSysCache1(OPEROID, ObjectIdGetDatum(operOid));
	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for operator__ %u", operOid);

	simple_heap_delete(relation, &tup->t_self);

	ReleaseSysCache(tup);

	heap_close(relation, RowExclusiveLock);
}


// Copyright (c) 1999-2008 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// http://www.digitalmars.com
// License for redistribution is by either the Artistic License
// in artistic.txt, or the GNU General Public License in gnu.txt.
// See the included readme.txt for details.

/* NOTE: This file has been patched from the original DMD distribution to
   work with the GDC compiler.

   Modified by David Friedman, September 2004
*/

#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <assert.h>

#include "mars.h"
#include "module.h"
#include "mtype.h"
#include "declaration.h"
#include "statement.h"
#include "enum.h"
#include "aggregate.h"
#include "init.h"
#include "attrib.h"
#include "id.h"
#include "import.h"
#include "template.h"

#include <mem.h>
#ifndef IN_GCC
#include "cc.h"
#include "global.h"
#include "oper.h"
#include "code.h"
#include "type.h"
#include "dt.h"
#include "cgcv.h"
#include "outbuf.h"
#include "irstate.h"
#else
#include "dt.h"
#endif

#ifdef IN_GCC
#include "d-dmd-gcc.h"
#endif

void obj_lzext(Symbol *s1,Symbol *s2);

#ifdef IN_GCC
#if 0
static bool m_in_a(Module * m, Array * a) {
    for (unsigned i = 0; i < a->dim; i++)
	if ( m == (Module *) a->data[i] )
	    return true;
    return false;
}
static void find_module_deps(Module * the_module, Array * out_deps)
{
    Array work;
    unsigned wi = 0;

    work.push(the_module);
    while (wi < work.dim) {
	Module * a_module = (Module *) work.data[wi];
	for (unsigned i = 0; i < a_module->aimports.dim; i++) {
	    Module * an_imp = (Module*) a_module->aimports.data[i];
	    if (! an_imp->needModuleInfo() ||
		m_in_a(an_imp, & work) || m_in_a(an_imp, out_deps))
		continue;
	    if (an_imp->strictlyneedmoduleinfo)
	    {
		out_deps->push(an_imp);
		fprintf(stderr, "idep: %s -> %s", the_module->toPrettyChars(),
		    an_imp->toPrettyChars());
		if (a_module != the_module)
		    fprintf(stderr, " (via %s)", a_module->toPrettyChars());
		fprintf(stderr, "\n");
	    }
	    else
		work.push(an_imp);
	}
	wi++;
    }
}
#endif
#endif


/* ================================================================== */

// Put out instance of ModuleInfo for this Module

void Module::genmoduleinfo()
{
    //printf("Module::genmoduleinfo() %s\n", toChars());

    Symbol *msym = toSymbol();
    unsigned offset;
    unsigned sizeof_ModuleInfo = 14 * PTRSIZE;

    //////////////////////////////////////////////

    csym->Sclass = SCglobal;
    csym->Sfl = FLdata;

    /* The layout is:
       {
	    void **vptr;
	    monitor_t monitor;
	    char[] name;		// class name
	    ModuleInfo importedModules[];
	    ClassInfo localClasses[];
	    uint flags;			// initialization state
	    void *ctor;
	    void *dtor;
	    void *unitTest;
	    const(MemberInfo[]) function(string) xgetMembers;	// module getMembers() function
	    void *ictor;
       }
     */
    dt_t *dt = NULL;

    if (moduleinfo)
	dtxoff(&dt, moduleinfo->toVtblSymbol(), 0, TYnptr); // vtbl for ModuleInfo
    else
    {	//printf("moduleinfo is null\n");
	dtdword(&dt, 0);		// BUG: should be an assert()
    }
    dtdword(&dt, 0);			// monitor

    // name[]
    char *name = toPrettyChars();
    size_t namelen = strlen(name);
    dtdword(&dt, namelen);
    dtabytes(&dt, TYnptr, 0, namelen + 1, name);

    ClassDeclarations aclasses;

    //printf("members->dim = %d\n", members->dim);
    for (int i = 0; i < members->dim; i++)
    {	Dsymbol *member = (Dsymbol *)members->data[i];

	//printf("\tmember '%s'\n", member->toChars());
	member->addLocalClass(&aclasses);
    }

    // importedModules[]
    int aimports_dim = aimports.dim;
#ifdef IN_GCC
#if 0
    Array adeps;

    if (! d_gcc_supports_weak())
    {
	find_module_deps(this, & adeps);
	aimports_dim = adeps.dim;
    }
    else
#endif
#endif
    for (int i = 0; i < aimports.dim; i++)
    {	Module *m = (Module *)aimports.data[i];
	if (!m->needModuleInfo())
	    aimports_dim--;
    }
    dtdword(&dt, aimports_dim);
    if (aimports.dim)
	dtxoff(&dt, csym, sizeof_ModuleInfo, TYnptr);
    else
	dtdword(&dt, 0);

    // localClasses[]
    dtdword(&dt, aclasses.dim);
    if (aclasses.dim)
	dtxoff(&dt, csym, sizeof_ModuleInfo + aimports_dim * PTRSIZE, TYnptr);
    else
	dtdword(&dt, 0);

    if (needmoduleinfo)
	dti32(&dt, 8|0, true);		// flags (4 means MIstandalone)
    else
	dti32(&dt, 8|4, true);		// flags (4 means MIstandalone)

    if (sctor)
	dtxoff(&dt, sctor, 0, TYnptr);
    else
	dtdword(&dt, 0);

    if (sdtor)
	dtxoff(&dt, sdtor, 0, TYnptr);
    else
	dtdword(&dt, 0);

    if (stest)
	dtxoff(&dt, stest, 0, TYnptr);
    else
	dtdword(&dt, 0);

    FuncDeclaration *sgetmembers = findGetMembers();
    if (sgetmembers)
	dtxoff(&dt, sgetmembers->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);			// xgetMembers

    if (sictor)
	dtxoff(&dt, sictor, 0, TYnptr);
    else
	dtdword(&dt, 0);

    //////////////////////////////////////////////

#ifdef IN_GCC
#if 0
    if (! d_gcc_supports_weak())
	for (i = 0; i < adeps.dim; i++)
	{
	    Module *m;
	    Symbol *s;
    
	    m = (Module *) adeps.data[i];
	    s = m->toSymbol();
	    s->Sflags |= SFLweak; // doesn't do anything yet, but see d-decls.cc:Module::toSymbol
	    dtxoff(&dt, s, 0, TYnptr);
	}
    else
#endif
#endif
    for (int i = 0; i < aimports.dim; i++)
    {	Module *m = (Module *)aimports.data[i];

	if (m->needModuleInfo())
	{   Symbol *s = m->toSymbol();
	    s->Sflags |= SFLweak;
	    dtxoff(&dt, s, 0, TYnptr);
	}
    }

    for (int i = 0; i < aclasses.dim; i++)
    {
	ClassDeclaration *cd = (ClassDeclaration *)aclasses.data[i];
	dtxoff(&dt, cd->toSymbol(), 0, TYnptr);
    }

    csym->Sdt = dt;
#if ELFOBJ
    // Cannot be CONST because the startup code sets flag bits in it
    csym->Sseg = DATA;
#endif
    outdata(csym);

    //////////////////////////////////////////////

    obj_moduleinfo(msym);
}

/* ================================================================== */
void Dsymbol::toObjFile(int multiobj)
{
    //printf("Dsymbol::toObjFile('%s')\n", toChars());
    // ignore
#ifdef IN_GCC
    TupleDeclaration * td = this->isTupleDeclaration();
    if (td)
    {	
	for (unsigned i = 0; i < td->objects->dim; i++)
	{
	    Object * o = (Object *) td->objects->data[i];
	    Expression * e;
	    Dsymbol *ds;
	    Declaration *d;
	    if ((o->dyncast() == DYNCAST_EXPRESSION) &&
		((Expression *) o)->op == TOKdsymbol)
	    {
		ds = ((DsymbolExp *) o)->s;
		d = ds->isDeclaration();
		if (d)
		    d->toObjFile(multiobj);
	    }
		
	}		
    }   
#endif
}
/* ================================================================== */

void ClassDeclaration::toObjFile(int multiobj)
{   unsigned i;
    unsigned offset;
    Symbol *sinit;
    enum_SC scclass;

    //printf("ClassDeclaration::toObjFile('%s')\n", toChars());

    if (!members)
	return;

    if (global.params.symdebug)
	toDebug();

    assert(!scope);	// semantic() should have been run to completion

    if (parent && parent->isTemplateInstance())
	scclass = SCcomdat;
    else
	scclass = SCglobal;

    // Put out the members
    for (i = 0; i < members->dim; i++)
    {
	Dsymbol *member;

	member = (Dsymbol *)members->data[i];
	member->toObjFile(0);
    }

#if 0
    // Build destructor by aggregating dtors[]
    Symbol *sdtor;
    switch (dtors.dim)
    {	case 0:
	    // No destructors for this class
	    sdtor = NULL;
	    break;

	case 1:
	    // One destructor, just use it directly
	    sdtor = ((DtorDeclaration *)dtors.data[0])->toSymbol();
	    break;

	default:
	{   /* Build a destructor that calls all the
	     * other destructors in dtors[].
	     */

	    elem *edtor = NULL;

	    // Declare 'this' pointer for our new destructor
	    Symbol *sthis = symbol_calloc("this");
	    sthis->Stype = type_fake(TYnptr);
	    sthis->Stype->Tcount++;
	    sthis->Sclass = SCfastpar;
	    sthis->Spreg = AX;
	    sthis->Sfl = FLauto;

	    // Call each of the destructors in dtors[]
	    // in reverse order
	    for (i = 0; i < dtors.dim; i++)
	    {	DtorDeclaration *d = (DtorDeclaration *)dtors.data[i];
		Symbol *s = d->toSymbol();
		elem *e;

		e = el_bin(OPcall, TYvoid, el_var(s), el_var(sthis));
		edtor = el_combine(e, edtor);
	    }

	    // Create type for the function
	    ::type *t = type_alloc(TYjfunc);
	    t->Tflags |= TFprototype | TFfixed;
	    t->Tmangle = mTYman_d;
	    t->Tnext = tsvoid;
	    tsvoid->Tcount++;

	    // Create the function, sdtor, and write it out
	    localgot = NULL;
	    sdtor = toSymbolX("__dtor", SCglobal, t, "FZv");
	    block *b = block_calloc();
	    b->BC = BCret;
	    b->Belem = edtor;
	    sdtor->Sfunc->Fstartblock = b;
	    cstate.CSpsymtab = &sdtor->Sfunc->Flocsym;
	    symbol_add(sthis);
	    writefunc(sdtor);
	}
    }
#endif

    // Generate C symbols
    toSymbol();
    toVtblSymbol();
    sinit = toInitializer();

    //////////////////////////////////////////////

    // Generate static initializer
    sinit->Sclass = scclass;
    sinit->Sfl = FLdata;
#if ELFOBJ // Burton
    sinit->Sseg = CDATA;
#endif /* ELFOBJ */
    toDt(&sinit->Sdt);
    outdata(sinit);

    //////////////////////////////////////////////

    // Put out the ClassInfo
    csym->Sclass = scclass;
    csym->Sfl = FLdata;

    /* The layout is:
       {
	    void **vptr;
	    monitor_t monitor;
	    byte[] initializer;		// static initialization data
	    char[] name;		// class name
	    void *[] vtbl;
	    Interface[] interfaces;
	    ClassInfo *base;		// base class
	    void *destructor;
	    void *invariant;		// class invariant
	    uint flags;
	    void *deallocator;
	    OffsetTypeInfo[] offTi;
	    void *defaultConstructor;
	    const(MemberInfo[]) function(string) xgetMembers;	// module getMembers() function
       }
     */
    dt_t *dt = NULL;
    offset = CLASSINFO_SIZE;			// must be ClassInfo.size
    if (classinfo)
    {
	if (classinfo->structsize != CLASSINFO_SIZE)
	    error("D compiler and phobos/object.d are mismatched");
    }

    if (classinfo)
	dtxoff(&dt, classinfo->toVtblSymbol(), 0, TYnptr); // vtbl for ClassInfo
    else
	dtdword(&dt, 0);		// BUG: should be an assert()
    dtdword(&dt, 0);			// monitor

    // initializer[]
    assert(structsize >= 8);
    dtdword(&dt, structsize);		// size
    dtxoff(&dt, sinit, 0, TYnptr);	// initializer

    // name[]
    char *name = ident->toChars();
    size_t namelen = strlen(name);
    if (!(namelen > 9 && memcmp(name, "TypeInfo_", 9) == 0))
    {	name = toPrettyChars();
	namelen = strlen(name);
    }
    dtdword(&dt, namelen);
    dtabytes(&dt, TYnptr, 0, namelen + 1, name);

    // vtbl[]
    dtdword(&dt, vtbl.dim);
    dtxoff(&dt, vtblsym, 0, TYnptr);

    // interfaces[]
    dtdword(&dt, vtblInterfaces->dim);
    if (vtblInterfaces->dim)
	dtxoff(&dt, csym, offset, TYnptr);	// (*)
    else
	dtdword(&dt, 0);

    // base
    if (baseClass)
	dtxoff(&dt, baseClass->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);

    // destructor
    if (dtor)
	dtxoff(&dt, dtor->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);

    // invariant
    if (inv)
	dtxoff(&dt, inv->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);

    // flags
    int flags = 16 | 4 | isCOMclass();
    if (ctor)
	flags |= 8;
    for (ClassDeclaration *cd = this; cd; cd = cd->baseClass)
    {
	if (cd->members)
	{
	    for (size_t i = 0; i < cd->members->dim; i++)
	    {
		Dsymbol *sm = (Dsymbol *)cd->members->data[i];
		//printf("sm = %s %s\n", sm->kind(), sm->toChars());
		if (sm->hasPointers())
		    goto L2;
	    }
	}
    }
    flags |= 2;			// no pointers
  L2:
    dti32(&dt, flags, true);


    // deallocator
    if (aggDelete)
	dtxoff(&dt, aggDelete->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);

    // offTi[]
    dtdword(&dt, 0);
    dtdword(&dt, 0);		// null for now, fix later

    // defaultConstructor
    if (defaultCtor)
	dtxoff(&dt, defaultCtor->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);

    FuncDeclaration *sgetmembers = findGetMembers();
    if (sgetmembers)
	dtxoff(&dt, sgetmembers->toSymbol(), 0, TYnptr);
    else
	dtdword(&dt, 0);	// module getMembers() function

    //////////////////////////////////////////////

    // Put out vtblInterfaces->data[]. Must immediately follow csym, because
    // of the fixup (*)

    offset += vtblInterfaces->dim * (4 * PTRSIZE);
    for (i = 0; i < vtblInterfaces->dim; i++)
    {	BaseClass *b = (BaseClass *)vtblInterfaces->data[i];
	ClassDeclaration *id = b->base;

	/* The layout is:
	 *  {
	 *	ClassInfo *interface;
	 *	void *[] vtbl;
	 *	unsigned offset;
	 *  }
	 */

	// Fill in vtbl[]
	b->fillVtbl(this, &b->vtbl, 1);

	dtxoff(&dt, id->toSymbol(), 0, TYnptr);		// ClassInfo

	// vtbl[]
	dtdword(&dt, id->vtbl.dim);
	dtxoff(&dt, csym, offset, TYnptr);

	dtdword(&dt, b->offset);			// this offset

	offset += id->vtbl.dim * PTRSIZE;
    }

    // Put out the vtblInterfaces->data[].vtbl[]
    // This must be mirrored with ClassDeclaration::baseVtblOffset()
    //printf("putting out %d interface vtbl[]s for '%s'\n", vtblInterfaces->dim, toChars());
    for (i = 0; i < vtblInterfaces->dim; i++)
    {	BaseClass *b = (BaseClass *)vtblInterfaces->data[i];
	ClassDeclaration *id = b->base;
	int j;

	//printf("    interface[%d] is '%s'\n", i, id->toChars());
	j = 0;
	if (id->vtblOffset())
	{
	    // First entry is ClassInfo reference
	    //dtxoff(&dt, id->toSymbol(), 0, TYnptr);

	    // First entry is struct Interface reference
	    dtxoff(&dt, csym, CLASSINFO_SIZE + i * (4 * PTRSIZE), TYnptr);
	    j = 1;
	}
	assert(id->vtbl.dim == b->vtbl.dim);
	for (; j < id->vtbl.dim; j++)
	{
	    FuncDeclaration *fd;

	    assert(j < b->vtbl.dim);
#if 0
	    Object *o = (Object *)b->vtbl.data[j];
	    if (o)
	    {
		printf("o = %p\n", o);
		assert(o->dyncast() == DYNCAST_DSYMBOL);
		Dsymbol *s = (Dsymbol *)o;
		printf("s->kind() = '%s'\n", s->kind());
	    }
#endif
	    fd = (FuncDeclaration *)b->vtbl.data[j];
	    if (fd)
		dtxoff(&dt, fd->toThunkSymbol(b->offset), 0, TYnptr);
	    else
		dtdword(&dt, 0);
	}
    }

#if 1
    // Put out the overriding interface vtbl[]s.
    // This must be mirrored with ClassDeclaration::baseVtblOffset()
    //printf("putting out overriding interface vtbl[]s for '%s' at offset x%x\n", toChars(), offset);
    ClassDeclaration *cd;
    Array bvtbl;

    for (cd = this->baseClass; cd; cd = cd->baseClass)
    {
	for (int k = 0; k < cd->vtblInterfaces->dim; k++)
	{   BaseClass *bs = (BaseClass *)cd->vtblInterfaces->data[k];

	    if (bs->fillVtbl(this, &bvtbl, 0))
	    {
		//printf("\toverriding vtbl[] for %s\n", bs->base->toChars());
		ClassDeclaration *id = bs->base;
		int j;

		j = 0;
		if (id->vtblOffset())
		{
		    // First entry is ClassInfo reference
		    //dtxoff(&dt, id->toSymbol(), 0, TYnptr);

		    // First entry is struct Interface reference
		    dtxoff(&dt, cd->toSymbol(), CLASSINFO_SIZE + k * (4 * PTRSIZE), TYnptr);
		    j = 1;
		}

		for (; j < id->vtbl.dim; j++)
		{
		    FuncDeclaration *fd;

		    assert(j < bvtbl.dim);
		    fd = (FuncDeclaration *)bvtbl.data[j];
		    if (fd)
			dtxoff(&dt, fd->toThunkSymbol(bs->offset), 0, TYnptr);
		    else
			dtdword(&dt, 0);
		}
	    }
	}
    }
#endif
#if INTERFACE_VIRTUAL
    // Put out the overriding interface vtbl[]s.
    // This must be mirrored with ClassDeclaration::baseVtblOffset()
    //printf("putting out overriding interface vtbl[]s for '%s' at offset x%x\n", toChars(), offset);
    for (i = 0; i < vtblInterfaces->dim; i++)
    {	BaseClass *b = (BaseClass *)vtblInterfaces->data[i];
	ClassDeclaration *cd;

	for (cd = this->baseClass; cd; cd = cd->baseClass)
	{
	    for (int k = 0; k < cd->vtblInterfaces->dim; k++)
	    {	BaseClass *bs = (BaseClass *)cd->vtblInterfaces->data[k];

		if (b->base == bs->base)
		{
		    //printf("\toverriding vtbl[] for %s\n", b->base->toChars());
		    ClassDeclaration *id = b->base;
		    int j;

		    j = 0;
		    if (id->vtblOffset())
		    {
			// First entry is ClassInfo reference
			//dtxoff(&dt, id->toSymbol(), 0, TYnptr);

			// First entry is struct Interface reference
			dtxoff(&dt, cd->toSymbol(), CLASSINFO_SIZE + k * (4 * PTRSIZE), TYnptr);
			j = 1;
		    }

		    for (; j < id->vtbl.dim; j++)
		    {
			FuncDeclaration *fd;

			assert(j < b->vtbl.dim);
			fd = (FuncDeclaration *)b->vtbl.data[j];
			if (fd)
			    dtxoff(&dt, fd->toThunkSymbol(bs->offset), 0, TYnptr);
			else
			    dtdword(&dt, 0);
		    }
		}
	    }
	}
    }
#endif


    csym->Sdt = dt;
#if ELFOBJ // Burton
    // ClassInfo cannot be const data, because we use the monitor on it
    csym->Sseg = DATA;
#endif /* ELFOBJ */
    outdata(csym);
    if (isExport())
	obj_export(csym,0);

    //////////////////////////////////////////////

    // Put out the vtbl[]
    //printf("putting out %s.vtbl[]\n", toChars());
    dt = NULL;
    if (0)
	i = 0;
    else
    {	dtxoff(&dt, csym, 0, TYnptr);		// first entry is ClassInfo reference
	i = 1;
    }
    for (; i < vtbl.dim; i++)
    {
	FuncDeclaration *fd = ((Dsymbol *)vtbl.data[i])->isFuncDeclaration();

	//printf("\tvtbl[%d] = %p\n", i, fd);
	if (fd && (fd->fbody || !isAbstract()))
	{
	    if (isFuncHidden(fd))
	    {
		if (global.params.warnings)
		{   fprintf(stdmsg, "warning - ");
		    error("%s %s is hidden in %s\n", fd->toParent()->toChars(), fd->toChars(), toChars());
		}
		dtxoff(&dt, rtlsym[RTLSYM_DHIDDENFUNC], 0, TYnptr);
	    }
	    else
		dtxoff(&dt, fd->toSymbol(), 0, TYnptr);
	}
	else
	    dtdword(&dt, 0);
    }
    vtblsym->Sdt = dt;
    vtblsym->Sclass = scclass;
    vtblsym->Sfl = FLdata;
#if ELFOBJ // Burton
    vtblsym->Sseg = CDATA;
#endif /* ELFOBJ */
    outdata(vtblsym);
    if (isExport())
	obj_export(vtblsym,0);
}

/******************************************
 * Get offset of base class's vtbl[] initializer from start of csym.
 * Returns ~0 if not this csym.
 */

unsigned ClassDeclaration::baseVtblOffset(BaseClass *bc)
{
    unsigned csymoffset;
    int i;

    //printf("ClassDeclaration::baseVtblOffset('%s', bc = %p)\n", toChars(), bc);
    csymoffset = CLASSINFO_SIZE;
    csymoffset += vtblInterfaces->dim * (4 * PTRSIZE);

    for (i = 0; i < vtblInterfaces->dim; i++)
    {
	BaseClass *b = (BaseClass *)vtblInterfaces->data[i];

	if (b == bc)
	    return csymoffset;
	csymoffset += b->base->vtbl.dim * PTRSIZE;
    }

#if 1
    // Put out the overriding interface vtbl[]s.
    // This must be mirrored with ClassDeclaration::baseVtblOffset()
    //printf("putting out overriding interface vtbl[]s for '%s' at offset x%x\n", toChars(), offset);
    ClassDeclaration *cd;
    Array bvtbl;

    for (cd = this->baseClass; cd; cd = cd->baseClass)
    {
	for (int k = 0; k < cd->vtblInterfaces->dim; k++)
	{   BaseClass *bs = (BaseClass *)cd->vtblInterfaces->data[k];

	    if (bs->fillVtbl(this, NULL, 0))
	    {
		if (bc == bs)
		{   //printf("\tcsymoffset = x%x\n", csymoffset);
		    return csymoffset;
		}
		csymoffset += bs->base->vtbl.dim * PTRSIZE;
	    }
	}
    }
#endif
#if INTERFACE_VIRTUAL
    for (i = 0; i < vtblInterfaces->dim; i++)
    {	BaseClass *b = (BaseClass *)vtblInterfaces->data[i];
	ClassDeclaration *cd;

	for (cd = this->baseClass; cd; cd = cd->baseClass)
	{
	    //printf("\tbase class %s\n", cd->toChars());
	    for (int k = 0; k < cd->vtblInterfaces->dim; k++)
	    {	BaseClass *bs = (BaseClass *)cd->vtblInterfaces->data[k];

		if (bc == bs)
		{   //printf("\tcsymoffset = x%x\n", csymoffset);
		    return csymoffset;
		}
		if (b->base == bs->base)
		    csymoffset += bs->base->vtbl.dim * PTRSIZE;
	    }
	}
    }
#endif

    return ~0;
}

/* ================================================================== */

void InterfaceDeclaration::toObjFile(int multiobj)
{   unsigned i;
    unsigned offset;
    Symbol *sinit;
    enum_SC scclass;

    //printf("InterfaceDeclaration::toObjFile('%s')\n", toChars());

    if (!members)
	return;

    if (global.params.symdebug)
	toDebug();

    if (parent && parent->isTemplateInstance())
	scclass = SCcomdat;
    else
	scclass = SCglobal;

    // Put out the members
    for (i = 0; i < members->dim; i++)
    {
	Dsymbol *member;

	member = (Dsymbol *)members->data[i];
	if (!member->isFuncDeclaration())
	    member->toObjFile(0);
    }

    // Generate C symbols
    toSymbol();

    //////////////////////////////////////////////

    // Put out the ClassInfo
    csym->Sclass = scclass;
    csym->Sfl = FLdata;

    /* The layout is:
       {
	    void **vptr;
	    monitor_t monitor;
	    byte[] initializer;		// static initialization data
	    char[] name;		// class name
	    void *[] vtbl;
	    Interface[] interfaces;
	    Object *base;		// base class
	    void *destructor;
	    void *invariant;		// class invariant
	    uint flags;
	    void *deallocator;
	    OffsetTypeInfo[] offTi;
	    void *defaultConstructor;
	    const(MemberInfo[]) function(string) xgetMembers;	// module getMembers() function
       }
     */
    dt_t *dt = NULL;

    if (classinfo)
	dtxoff(&dt, classinfo->toVtblSymbol(), 0, TYnptr); // vtbl for ClassInfo
    else
	dtdword(&dt, 0);		// BUG: should be an assert()
    dtdword(&dt, 0);			// monitor

    // initializer[]
    dtdword(&dt, 0);			// size
    dtdword(&dt, 0);			// initializer

    // name[]
    char *name = toPrettyChars();
    size_t namelen = strlen(name);
    dtdword(&dt, namelen);
    dtabytes(&dt, TYnptr, 0, namelen + 1, name);

    // vtbl[]
    dtdword(&dt, 0);
    dtdword(&dt, 0);

    // vtblInterfaces->data[]
    dtdword(&dt, vtblInterfaces->dim);
    if (vtblInterfaces->dim)
    {
	if (classinfo)
	    assert(classinfo->structsize == CLASSINFO_SIZE);
	offset = CLASSINFO_SIZE;
	dtxoff(&dt, csym, offset, TYnptr);	// (*)
    }
    else
	dtdword(&dt, 0);

    // base
    assert(!baseClass);
    dtdword(&dt, 0);

    // dtor
    dtdword(&dt, 0);

    // invariant
    dtdword(&dt, 0);

    // flags
    dti32(&dt, 4 | isCOMinterface(), true);

    // deallocator
    dtdword(&dt, 0);

    // offTi[]
    dtdword(&dt, 0);
    dtdword(&dt, 0);		// null for now, fix later

    // defaultConstructor
    dtdword(&dt, 0);

    // xgetMembers
    dtdword(&dt, 0);

    //////////////////////////////////////////////

    // Put out vtblInterfaces->data[]. Must immediately follow csym, because
    // of the fixup (*)

    offset += vtblInterfaces->dim * (4 * PTRSIZE);
    for (i = 0; i < vtblInterfaces->dim; i++)
    {	BaseClass *b = (BaseClass *)vtblInterfaces->data[i];
	ClassDeclaration *id = b->base;

	// ClassInfo
	dtxoff(&dt, id->toSymbol(), 0, TYnptr);

	// vtbl[]
	dtdword(&dt, 0);
	dtdword(&dt, 0);

	// this offset
	dtdword(&dt, b->offset);
    }

    csym->Sdt = dt;
#if ELFOBJ // Burton
    csym->Sseg = CDATA;
#endif /* ELFOBJ */
    outdata(csym);
    if (isExport())
	obj_export(csym,0);
}

/* ================================================================== */

void StructDeclaration::toObjFile(int multiobj)
{   unsigned i;

    //printf("StructDeclaration::toObjFile('%s')\n", toChars());

    // Anonymous structs/unions only exist as part of others,
    // do not output forward referenced structs's
    if (!isAnonymous() && members)
    {
	if (global.params.symdebug)
	    toDebug();

	type->getTypeInfo(NULL);	// generate TypeInfo

	if (1)
	{
	    // Generate static initializer
	    toInitializer();
#if 0
	    sinit->Sclass = SCcomdat;
#else
	    if (parent && parent->isTemplateInstance())
		sinit->Sclass = SCcomdat;
	    else
		sinit->Sclass = SCglobal;
#endif
	    sinit->Sfl = FLdata;
	    toDt(&sinit->Sdt);

#if !ELFOBJ
	    /* For OMF, common blocks aren't pulled in from the library.
	     */
	    /* ELF comdef's generate multiple
	     * definition errors for them from the gnu linker.
	     * Need to figure out how to generate proper comdef's for ELF.
	     */
	    // See if we can convert a comdat to a comdef,
	    // which saves on exe file space.
	    if (sinit->Sclass == SCcomdat &&
		sinit->Sdt &&
		sinit->Sdt->dt == DT_azeros &&
		sinit->Sdt->DTnext == NULL)
	    {
		sinit->Sclass = SCglobal;
		sinit->Sdt->dt = DT_common;
	    }
#endif

#if ELFOBJ // Burton
	    sinit->Sseg = CDATA;
#endif
	    outdata(sinit);
	}

	// Put out the members
	for (i = 0; i < members->dim; i++)
	{
	    Dsymbol *member;

	    member = (Dsymbol *)members->data[i];
	    member->toObjFile(0);
	}
    }
}

/* ================================================================== */

void VarDeclaration::toObjFile(int multiobj)
{
    Symbol *s;
    unsigned sz;
    Dsymbol *parent;

    //printf("VarDeclaration::toObjFile(%p '%s' type=%s) protection %d\n", this, toChars(), type->toChars(), protection);
    //printf("\talign = %d\n", type->alignsize());

    if (aliassym)
    {	toAlias()->toObjFile(0);
	return;
    }

    // Do not store variables we cannot take the address of
    if (!canTakeAddressOf())
    {
	return;
    }

    if (isDataseg() && !(storage_class & STCextern))
    {
	s = toSymbol();
	sz = type->size();

	parent = this->toParent();
#if 0	/* private statics should still get a global symbol, in case
	 * another module inlines a function that references it.
	 */
	if (/*protection == PROTprivate ||*/
	    !parent || parent->ident == NULL || parent->isFuncDeclaration())
	{
	    s->Sclass = SCstatic;
	}
	else
#endif
	{
	    if (storage_class & STCcomdat)
		s->Sclass = SCcomdat;
	    else
		s->Sclass = SCglobal;

            do
            {
                /* Global template data members need to be in comdat's
                 * in case multiple .obj files instantiate the same
                 * template with the same types.
                 */
                if (parent->isTemplateInstance() && !parent->isTemplateMixin())
                {
#if 0
                    /* These symbol constants have already been copied,
                     * so no reason to output them.
                     * Note that currently there is no way to take
                     * the address of such a const.
                     */
                    if (isConst() && type->toBasetype()->ty != Tsarray &&
                        init && init->isExpInitializer())
                        return;
#endif
                    s->Sclass = SCcomdat;
                    break;
                }
                parent = parent->parent;
            } while (parent);
	}
	s->Sfl = FLdata;

	if (init)
	{   s->Sdt = init->toDt();

	    // Look for static array that is block initialized
	    Type *tb;
	    ExpInitializer *ie = init->isExpInitializer();

	    tb = type->toBasetype();
	    if (tb->ty == Tsarray && ie &&
		!tb->nextOf()->equals(ie->exp->type->toBasetype()->nextOf()) &&
		ie->exp->implicitConvTo(tb->nextOf())
		)
	    {
		int dim;

		dim = ((TypeSArray *)tb)->dim->toInteger();

		// Duplicate Sdt 'dim-1' times, as we already have the first one
		while (--dim > 0)
		{
		    ie->exp->toDt(&s->Sdt);
		}
	    }
	}
	else if (storage_class & STCextern)
	{
	    s->Sclass = SCextern;
	    s->Sfl = FLextern;
	    s->Sdt = NULL;
	    // BUG: if isExport(), shouldn't we make it dllimport?
	    return;
	}
	else
	{
	    type->toDt(&s->Sdt);
	}
	dt_optimize(s->Sdt);

	// See if we can convert a comdat to a comdef,
	// which saves on exe file space.
	if (s->Sclass == SCcomdat &&
	    s->Sdt &&
	    s->Sdt->dt == DT_azeros &&
	    s->Sdt->DTnext == NULL)
	{
	    s->Sclass = SCglobal;
	    s->Sdt->dt = DT_common;
	}

#if ELFOBJ // Burton
	if (s->Sdt && s->Sdt->dt == DT_azeros && s->Sdt->DTnext == NULL)
	    s->Sseg = UDATA;
	else
	    s->Sseg = DATA;
#endif /* ELFOBJ */
	if (sz)
	{   outdata(s);
	    if (isExport())
		obj_export(s,0);
	}
    }
#ifdef IN_GCC
    else {
	// This is needed for VarDeclarations in mixins that are to be
	// local variables of a function.  Otherwise, it would be
	// enough to make a check for isVarDeclaration() in
	// DeclarationExp::toElem.
	d_gcc_emit_local_variable(this);
    }
#endif
}

/* ================================================================== */

void TypedefDeclaration::toObjFile(int multiobj)
{
    //printf("TypedefDeclaration::toObjFile('%s')\n", toChars());

    if (global.params.symdebug)
	toDebug();

    type->getTypeInfo(NULL);	// generate TypeInfo

    TypeTypedef *tc = (TypeTypedef *)type;
    if (type->isZeroInit() || !tc->sym->init)
	;
    else
    {
	enum_SC scclass = SCglobal;
	for (Dsymbol *parent = this->parent; parent; parent = parent->parent)
	{
	    if (parent->isTemplateInstance())
	    {
		scclass = SCcomdat;
		break;
	    }
	}

	// Generate static initializer
	toInitializer();
	sinit->Sclass = scclass;
	sinit->Sfl = FLdata;
#if ELFOBJ // Burton
	sinit->Sseg = CDATA;
#endif /* ELFOBJ */
	if (! sinit->Sdt)
	    sinit->Sdt = tc->sym->init->toDt();
	outdata(sinit);
    }
}

/* ================================================================== */

void EnumDeclaration::toObjFile(int multiobj)
{
    //printf("EnumDeclaration::toObjFile('%s')\n", toChars());

    if (isAnonymous())
	return;

    if (global.params.symdebug)
	toDebug();

    type->getTypeInfo(NULL);	// generate TypeInfo

    TypeEnum *tc = (TypeEnum *)type;
    if (!tc->sym->defaultval || type->isZeroInit())
	;
    else
    {
	enum_SC scclass = SCglobal;
	for (Dsymbol *parent = this->parent; parent; parent = parent->parent)
	{
	    if (parent->isTemplateInstance())
	    {
		scclass = SCcomdat;
		break;
	    }
	}

	// Generate static initializer
	toInitializer();
	sinit->Sclass = scclass;
	sinit->Sfl = FLdata;
#if ELFOBJ // Burton
	sinit->Sseg = CDATA;
#endif /* ELFOBJ */
	tc->sym->defaultval->toDt(&sinit->Sdt);
	//sinit->Sdt = tc->sym->init->toDt();
	outdata(sinit);
    }
}



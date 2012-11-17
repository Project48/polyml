(*
    Copyright (c) 2009 David C. J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*)

functor TYPEIDCODE (
    structure LEX : LEXSIG;
    structure CODETREE : CODETREESIG
    structure TYPETREE : TYPETREESIG

    structure STRUCTVALS : STRUCTVALSIG;

    structure DEBUG :
    sig
        val printDepthFunTag : (unit->int) Universal.tag
        val errorDepthTag: int Universal.tag
        val getParameter :
           'a Universal.tag -> Universal.universal list -> 'a
    end;

    structure PRETTY : PRETTYSIG;

    structure ADDRESS :
    sig
        type machineWord;
        type address;   (* an object that's represented as a pointer *) 
        type short = Word.word;   (* an object that's represented as a 30-bit int *)
        val toMachineWord : 'a    -> machineWord;
        val toAddress : 'a -> address;
        val F_words     : Word8.word;
        val F_bytes     : Word8.word;
        val F_mutable   : Word8.word;
        val isShort   : 'a -> bool;
        val loadWord:   (address * short) -> machineWord;
    end;
    
    sharing LEX.Sharing = STRUCTVALS.Sharing = PRETTY.Sharing = CODETREE.Sharing
            = TYPETREE.Sharing = ADDRESS
) : TYPEIDCODESIG =
struct
    open CODETREE PRETTY ADDRESS STRUCTVALS TYPETREE
    open RuntimeCalls
    
    (* This module deals with handling the run-time values that carry type
       information.  At the moment that's just the equality and print
       operations but that will be extended.
       
       There are different versions according to whether this is a
       monomorphic constructor, a polymorphic constructor or a type.
       Monomorphic and polymorphic constructor values are passed around
       in the module system as run-time values for types and datatypes
       whereas type values are passed in the core language as an extra
       argument to polymorphic functions.

       Both monomorphic and polymorphic constructors contain a reference
       for the "printer" entry so that a pretty printer can be installed.
       The functions in polymorphic datatypes have to be applied to type
       values for the base types to construct a type value.  Monomorphic
       datatypes just need some transformation.
       The effective types in each case are
       PolyType : (T('a) -> <'a t, 'a t> -> bool) * (T('a) -> 'a t * int -> pretty) ref
       MonoType : (<t * t> -> bool) * (t * int -> pretty) ref
       Type:      (<t * t> -> bool) * (t * int -> pretty)
       where < > denotes multiple (poly-style) arguments rather than tuples.
       *)

    (* If this is true we are just using additional arguments for equality type
       variables.  If false we are using them for all type variables and every
       polymorphic function is wrapped in a function that passes the type
       information. *)
    val justForEqualityTypes = true

    val arg1     = mkLoad (~1, 0) (* Used frequently. *)
    val arg2     = mkLoad (~2, 0)

    val InternalError = Misc.InternalError

    val orb = Word8.orb
    infix 7 orb;
    val mutableFlags = F_words orb F_mutable;

    (* codeStruct and codeAccess are copied from ValueOps. *)
    fun codeStruct (str, level) =
        if isUndefinedStruct str
        then CodeNil
        else codeAccess (structAccess str, level)

    and codeAccess (Global code, _) = code
      
    |   codeAccess (Local{addr=ref locAddr, level=ref locLevel}, level) =
            mkLoad (locAddr, level - locLevel) (* No need for the recursive case. *)
     
    |   codeAccess (Selected{addr, base}, level) =
            mkInd (addr, codeStruct (base, level))
     
    |   codeAccess _ = raise InternalError "No access"

    (* Load an identifier. *)
    fun codeId(TypeId{access, ...}, level) = codeAccess(access, level)
    (* Pretty printer code.  These produce code to apply the pretty printer functions. *)
    fun codePrettyString(s: string) = mkConst(toMachineWord(PrettyString s))
    and codePrettyBreak(n, m) = mkConst(toMachineWord(PrettyBreak(n, m)))

    and codePrettyBlock(n: int, t: bool, c: context list, args: codetree) =
        mkEval(mkConst(toMachineWord(PrettyBlock)),
            [mkTuple[mkConst(toMachineWord n), mkConst(toMachineWord t),
             mkConst(toMachineWord c), args]])

    (* Turn a list of codetrees into a run-time list. *)
    and codeList(c: codetree list, tail: codetree): codetree =
        List.foldr (fn (hd, tl) => mkTuple[hd, tl]) tail c

    (* Generate code to check that the depth is not less than the allowedDepth
       and if it is to print "..." rather than the given code. *)
    and checkDepth(depthCode: codetree, allowedDepth: int, codeOk, codeFail) =
        mkIf(
            mkEval(
                rtsFunction POLY_SYS_int_lss,
                [depthCode, mkConst(toMachineWord allowedDepth)]),
            codeFail,
            codeOk)

    (* Subtract one from the current depth to produce the depth for sub-elements. *)
    and decDepth depthCode =
        mkEval(rtsFunction POLY_SYS_aminus, [depthCode, mkConst(toMachineWord 1)])

    structure TypeVarMap =
    struct
        (* Entries are either type var maps or "stoppers". *)
        datatype typeVarMapEntry =
            TypeVarFormEntry of (typeVarForm * (int->codetree)) list
        |   TypeConstrListEntry of typeConstrs list

        type typeVarMap =
        {
            entryType: typeVarMapEntry, (* Either the type var map or a "stopper". *)
            cache: (* Cache of new type values. *)
                {typeOf: types, address: int, decCode: codeBinding} list ref,
            mkAddr: int->int, (* Make new addresses at this level. *)
            level: int (* Function nesting level. *)
        } list

        (* Default map. *)
        fun defaultTypeVarMap (mkAddr, level) = [{entryType=TypeConstrListEntry[], cache=ref [], mkAddr=mkAddr, level=level}]

        fun markTypeConstructors(typConstrs, mkAddr, level, tvs) =
                {entryType = TypeConstrListEntry typConstrs, cache = ref [], mkAddr=mkAddr, level=level} :: tvs

        fun getCachedTypeValues(({cache=ref cached, ...}) ::_): codeBinding list =
                (* Extract the values from the list.  The later values may refer to earlier
                   so the list must be reversed. *)
                List.rev (List.map (fn{decCode, ...} => decCode) cached)
        |   getCachedTypeValues _ = raise Misc.InternalError "getCachedTypeValues"

        (* Extend a type variable environment with a new map of type variables to load functions. *)
        fun extendTypeVarMap (tvMap: (typeVarForm * (int->codetree)) list, mkAddr, level, typeVarMap) =
            {entryType = TypeVarFormEntry tvMap, cache = ref [], mkAddr=mkAddr, level=level} :: typeVarMap

        (* If we find the type var in the map return it as a type.  This is used to
           eliminate apparently generalisable type vars from the list. *)
        fun mapTypeVars [] _ = NONE

        |   mapTypeVars ({entryType=TypeVarFormEntry typeVarMap, ...} :: rest) tyVar =
            (
            case List.find(fn(t, _) => sameTv(t, tyVar)) typeVarMap of
                SOME (tv, _) => SOME(TypeVar tv)
            |   NONE => mapTypeVars rest tyVar
            )

        |   mapTypeVars (_ :: rest) tyVar = mapTypeVars rest tyVar

        (* Check to see if a type constructor is in the "stopper" set and return the level
           if it is. *)
        fun checkTypeConstructor(_, []) = ~1 (* Not there. *)
        |   checkTypeConstructor(tyCons, {entryType=TypeVarFormEntry _, ...} :: rest) =
                checkTypeConstructor(tyCons, rest: typeVarMap)
        |   checkTypeConstructor(tyCons, {entryType=TypeConstrListEntry tConstrs, ...} :: rest) =
                if List.exists(fn t => sameTypeId(tcIdentifier t, tcIdentifier tyCons)) tConstrs
                then List.length rest + 1
                else checkTypeConstructor(tyCons, rest)

        local
            open TypeValue
            (* The printer and equality functions must be valid functions even when they
               will never be called.  We may have to construct dummy type values
               by applying a polymorphic type constructor to them and if
               they don't have the right form the optimiser will complain.
               If we're only using type values for equality type variables the default
               print function will be used in polymorphic functions so must print "?". *)
            val codePrintDefault = mkProc(codePrettyString "?", 1, "print-default")
            val errorFunction2 = mkProc(CodeZero, 2, "errorCode2")
            val codeFn = mkProc(codePrettyString "fn", 1, "print-function")

            local
                fun typeValForMonotype typConstr =
                let
                    val codedId = codeId(tcIdentifier typConstr, 0)
                    val printerRefAddress = extractPrinter codedId
                    val printFn = (* Create a function to load the printer ref and apply to the args. *)
                        mkProc(
                            mkEval(
                                mkEval(rtsFunction POLY_SYS_load_word, [printerRefAddress, CodeZero]),
                                [arg1]),
                            1, "print-" ^ tcName typConstr)
                in
                    createTypeValue{
                        eqCode=extractEquality codedId, printCode=printFn,
                        boxedCode=extractBoxed codedId, sizeCode=extractSize codedId}
                end
            in
                (* A few common types.  These are effectively always cached. *)
                val intCode    = typeValForMonotype intConstr
                and boolCode   = typeValForMonotype boolConstr
                and stringCode = typeValForMonotype stringConstr
                and charCode   = typeValForMonotype charConstr
            end

            (* Code generate this now so we only get one entry. *)
            val codeTuple =
                mkTuple[
                    createTypeValue{ (* Unused type variable. *)
                        eqCode=errorFunction2, printCode=codePrintDefault, boxedCode=boxedEither, sizeCode=singleWord},
                    createTypeValue{ (* Function. *)
                        eqCode=errorFunction2, printCode=codeFn, boxedCode=boxedAlways, sizeCode=singleWord},
                    intCode, boolCode, stringCode, charCode
                ]
            val code = genCode(codeTuple, [])()
        in
            (* Default code used for a type variable that is not referenced but
               needs to be provided to satisfy the type. *)
            val defaultTypeCode = mkInd(0, code)
            val functionCode = mkInd(1, code)
            val cachedCode = [(intConstr, mkInd(2, code)), (boolConstr, mkInd(3, code)),
                              (stringConstr, mkInd(4, code)), (charConstr, mkInd(5, code))]
        end

        fun findCachedTypeCode(typeVarMap, typ): ((int->codetree) * int) option =
        let
            (* Test if we have the same type as the cached type. *)
            fun sameType (t1, t2) =
                case (eventual t1, eventual t2) of
                    (TypeVar tv1, TypeVar tv2) =>
                    (
                        case (tvValue tv1, tvValue tv2) of
                            (EmptyType, EmptyType) => sameTv(tv1, tv2)
                        |   _ => false
                    )
                |   (FunctionType{arg=arg1, result=result1}, FunctionType{arg=arg2, result=result2}) =>
                        sameType(arg1, arg2) andalso sameType(result1, result2)

                |   (LabelledType{recList=list1, ...}, LabelledType{recList=list2, ...}) =>
                        ListPair.allEq(
                            fn({name=n1, typeof=t1}, {name=n2, typeof=t2}) => n1 = n2 andalso sameType(t1, t2))
                            (list1, list2)
                
                |   (TypeConstruction{constr=c1, args=a1, ...}, TypeConstruction{constr=c2, args=a2, ...}) =>
                        sameTypeConstr(c1, c2) andalso ListPair.allEq sameType (a1, a2)

                |   _ => false

            and sameTypeConstr(tc1, tc2) = sameTypeId(tcIdentifier tc1, tcIdentifier tc2)


            fun findCodeFromCache([], _) = NONE
            |   findCodeFromCache({cache=ref cache, level, ...} :: rest, ty) =
                (
                    case List.find(fn {typeOf, ...} => sameType(typeOf, ty)) cache of
                        NONE => findCodeFromCache(rest, ty)
                    |   SOME{address, ...} => SOME(fn l => mkLoad(address, l-level), List.length rest +1)
                )
        in
            case typ of
                TypeVar tyVar =>
                (
                    case tvValue tyVar of
                        EmptyType =>
                        let (* If it's a type var it is either in the type var list or we return the
                               default.  It isn't in the cache. *)
                            fun findCodeFromTypeVar([], _) = ((fn _ => defaultTypeCode), 0)
                                (* Return default code for a missing type variable.  This can occur
                                   if we have unreferenced type variables that need to be supplied but
                                   are treated as "don't care". *)

                            |   findCodeFromTypeVar({entryType=TypeVarFormEntry typeVarMap, ...} :: rest, tyVar) =
                                (
                                case List.find(fn(t, _) => sameTv(t, tyVar)) typeVarMap of
                                    SOME(_, codeFn) => (codeFn, List.length rest+1)
                                |   NONE => findCodeFromTypeVar(rest, tyVar)
                                )

                            |   findCodeFromTypeVar(_ :: rest, tyVar) = findCodeFromTypeVar(rest, tyVar)
                        in
                            SOME(findCodeFromTypeVar(typeVarMap, tyVar))
                        end

                    |   OverloadSet _ =>
                        let
                            val constr = typeConstrFromOverload(typ, false)
                        in
                            findCachedTypeCode(typeVarMap, mkTypeConstruction(tcName constr, constr, [], []))
                        end

                    |   ty => findCachedTypeCode(typeVarMap, ty)
                )

            |   TypeConstruction { constr, args, ...} =>
                    let
                        fun sameTypeConstr(tc1, tc2) = sameTypeId(tcIdentifier tc1, tcIdentifier tc2)
                    in
                        if tcIsAbbreviation constr (* Type abbreviation *)
                        then findCachedTypeCode(typeVarMap, makeEquivalent (constr, args))
                        else if null args
                        then (* Check the permanently cached monotypes. *)
                            case List.find(fn (t, _) => sameTypeConstr(t, constr)) cachedCode of
                                SOME (_, c) => SOME ((fn _ => c), ~1)
                            |   NONE => findCodeFromCache(typeVarMap, typ)
                        else findCodeFromCache(typeVarMap, typ)
                    end

            |   FunctionType _ => SOME(fn _ => functionCode, ~1) (* Every function has the same code. *)

            |   _ => findCodeFromCache(typeVarMap, typ)
        end

    end

    open TypeVarMap

    (* Find the earliest entry in the cache table where we can put this entry. *)
    fun getMaxDepth typeVarMap (ty: types, maxSoFar:int) : int =
        case findCachedTypeCode(typeVarMap, ty) of
            SOME (_, cacheDepth) => Int.max(cacheDepth, maxSoFar)
        |   NONE =>
            let
            in
                case ty of
                    TypeVar tyVar =>
                    (
                        case tvValue tyVar of
                            OverloadSet _ => maxSoFar (* Overloads are all global. *)
                        |   EmptyType => maxSoFar
                        |   tyVal => getMaxDepth typeVarMap (tyVal, maxSoFar)
                    )

                |   TypeConstruction{constr, args, ...} =>
                        if tcIsAbbreviation constr  (* May be an alias *)
                        then getMaxDepth typeVarMap (makeEquivalent (constr, args), maxSoFar)
                        else List.foldl (getMaxDepth typeVarMap)
                                       (Int.max(maxSoFar, checkTypeConstructor(constr, typeVarMap))) args

                |   LabelledType {recList, ...} =>
                        List.foldl (fn ({typeof, ...}, m) =>
                                getMaxDepth typeVarMap (typeof, m)) maxSoFar recList

                |   _ => maxSoFar
            end

    (* Create a look-up function for type variables in a datatype or type constructor.
       This has to create a full type value even though only one field of it will be
       used but that maintains consistency with the rest of the typeVarMap. *)
    fun mkTcArgMap (argTypes, level) =
        let
            val nArgs = List.length argTypes
            val argAddrs = List.tabulate(nArgs, fn n => n-nArgs)
            val args = List.map(fn addr => fn l => mkLoad(addr, l-level)) argAddrs
        in
            (ListPair.zipEq(argTypes, args), List.map (fn addr => mkLoad(addr, (*0*)1)) argAddrs)
        end

    (* Get the boxedness status for a type i.e. whether values of the type are always addresses,
       always tagged integers or could be either. *)
    fun boxednessForType(ty, level, getTypeValueForID, typeVarMap): codetree =
        case findCachedTypeCode(typeVarMap, ty) of
            SOME (code, _) => TypeValue.extractBoxed(code level)
        |   NONE =>
            let
                fun boxednessForConstruction(constr, args): codetree =
                (* Get the boxedness for a datatype construction. *)
                let
                    (* Get the boxedness functions for the argument types.
                       This applies only to polytypes. *)
                    fun getArg ty : codetree =
                    let
                        val boxedFun = boxednessForType(ty, level, getTypeValueForID, typeVarMap)
                        open TypeValue
                    in
                        (* We need a type value here although only the boxedFun will be used. *)
                        createTypeValue{eqCode=CodeZero, printCode=CodeZero, boxedCode=boxedFun, sizeCode=singleWord}
                    end

                    val codeForId =
                        TypeValue.extractBoxed(getTypeValueForID(tcIdentifier constr, args, level))
                in
                    (* Apply the function we obtained to any type arguments. *)
                    if null args then codeForId else mkEval(codeForId, map getArg args)
                end
            in
                case ty of
                    TypeVar tyVar =>
                    (
                        case tvValue tyVar of
                            OverloadSet _ => boxednessForConstruction(typeConstrFromOverload(ty, false), [])
                        |   EmptyType => raise InternalError "boxedness: should already have been handled"
                        |   tyVal => boxednessForType(tyVal, level, getTypeValueForID, typeVarMap)
                    )

                |   TypeConstruction{constr, args, ...} =>
                        if tcIsAbbreviation constr  (* May be an alias *)
                        then boxednessForType (makeEquivalent (constr, args), level, getTypeValueForID, typeVarMap)
                        else boxednessForConstruction(constr, args)

                |   LabelledType {recList=[{typeof=singleton, ...}], ...} =>
                        (* Unary tuples are optimised - no indirection. *)
                        boxednessForType(singleton, level, getTypeValueForID, typeVarMap)

                |   LabelledType _ => TypeValue.boxedAlways (* Tuple are currently always boxed. *)

                    (* Functions are handled in the cache case. *)
                |   _ => raise InternalError "boxednessForType: Unknown type"
            end

    (* Get the size for values of the type.  A value N other than 1 means that every value of the
       type is a pointer to a tuple of exactly N words.  Zero is never used.  *)
    fun sizeForType(ty, level, getTypeValueForID, typeVarMap): codetree =
        case findCachedTypeCode(typeVarMap, ty) of
            SOME (code, _) => TypeValue.extractSize(code level)
        |   NONE =>
            let
                fun sizeForConstruction(constr, args): codetree =
                (* Get the size for a datatype construction. *)
                let
                    (* Get the size functions for the argument types.
                       This applies only to polytypes. *)
                    fun getArg ty : codetree =
                    let
                        val sizeFun = sizeForType(ty, level, getTypeValueForID, typeVarMap)
                        open TypeValue
                    in
                        (* We need a type value here although only the sizeFun will be used. *)
                        createTypeValue{eqCode=CodeZero, printCode=CodeZero, boxedCode=CodeZero, sizeCode=sizeFun}
                    end

                    val codeForId =
                        TypeValue.extractSize(getTypeValueForID(tcIdentifier constr, args, level))
                in
                    (* Apply the function we obtained to any type arguments. *)
                    if null args then codeForId else mkEval(codeForId, map getArg args)
                end
            in
                case ty of
                    TypeVar tyVar =>
                    (
                        case tvValue tyVar of
                            OverloadSet _ => sizeForConstruction(typeConstrFromOverload(ty, false), [])
                        |   EmptyType => raise InternalError "size: should already have been handled"
                        |   tyVal => sizeForType(tyVal, level, getTypeValueForID, typeVarMap)
                    )

                |   TypeConstruction{constr, args, ...} =>
                        if tcIsAbbreviation constr  (* May be an alias *)
                        then sizeForType (makeEquivalent (constr, args), level, getTypeValueForID, typeVarMap)
                        else sizeForConstruction(constr, args)

                |   LabelledType {recList=[{typeof=singleton, ...}], ...} =>
                        (* Unary tuples are optimised - no indirection. *)
                        sizeForType(singleton, level, getTypeValueForID, typeVarMap)

                |   LabelledType{recList, ...} =>
                    let
                        val length = List.length recList
                    in
                        (* Set the length to the number of words that can be unpacked.
                           If there are more than 4 items it's probably not worth packing
                           them into other tuples so set this to one. *)
                        if length <= 4 (*!maxPacking*)
                        then mkConst(toMachineWord length)
                        else TypeValue.singleWord
                    end

                    (* Functions are handled in the cache case. *)
                |   _ => raise InternalError "sizeForType: Unknown type"
            end

    fun printerForType(ty, baseLevel, argTypes: typeVarMap) =
    let
        fun printCode(typ, level) =
            (
                case typ of
                    typ as TypeVar tyVar =>
                    (
                        case tvValue tyVar of
                            EmptyType =>
                            (
                                case findCachedTypeCode(argTypes, typ) of
                                    SOME (code, _) => TypeValue.extractPrinter(code level)
                                |   NONE => raise InternalError "printerForType: should already have been handled"
                            )

                        |   OverloadSet _ =>
                            let
                                val constr = typeConstrFromOverload(typ, false)
                            in
                                printCode(mkTypeConstruction(tcName constr, constr, [], []), level)
                            end

                        |   _ =>  (* Just a bound type variable. *) printCode(tvValue tyVar, level)
                    )

                |   TypeConstruction { constr=typConstr, args, name, ...} =>
                        if tcIsAbbreviation typConstr (* Handle type abbreviations directly *)
                        then printCode(makeEquivalent (typConstr, args), level)
                        else
                        let
                            (* Get the type Id and put in code to extract the printer ref. *)
                            val codedId = codeId(tcIdentifier typConstr, level+1)
                            open TypeValue
                            val printerRefAddress = extractPrinter codedId
                            (* We need a type value here.  The printer field will be used to
                               print the type argument and the boxedness and size fields may
                               be needed to extract the argument from the constructed value. *)
                            fun makePrinterId t =
                            let
                                fun codeForId(typeId, _, l) = codeId(typeId, l)
                            in
                                createTypeValue
                                    {eqCode=CodeZero, printCode=printCode(t, level+1),
                                     boxedCode=boxednessForType(t, level+1, codeForId, argTypes),
                                     sizeCode=sizeForType(t, level+1, codeForId, argTypes)}
                            end

                            val argList = map makePrinterId args
                        in
                            case args of
                                [] => (* Create a function that, when called, will extract the function from
                                         the reference and apply it the pair of the value and the depth. *)
                                    mkProc(
                                        mkEval(
                                            mkEval(rtsFunction POLY_SYS_load_word, [printerRefAddress, CodeZero]),
                                            [arg1]),
                                        1, "print-"^name)
                            |   _ =>  (* Construct a function, that when called, will extract the
                                         function from the reference and apply it first to the
                                         base printer functions and then to the pair of the value and depth. *)
                                    mkProc(
                                        mkEval(
                                            mkEval(
                                                mkEval(rtsFunction POLY_SYS_load_word, [printerRefAddress, CodeZero]),
                                                argList),
                                            [arg1]),
                                        1, "print-"^name)
                        end

                |   LabelledType { recList=[], ...} =>
                        (* Empty tuple: This is the unit value. *) mkProc(codePrettyString "()", 1, "print-labelled")

                |   LabelledType (r as { recList, ...}) =>
                    let
                        (* See if this has fields numbered 1=, 2= etc.   N.B.  If it has only one field
                           we need to print 1= since we don't have singleton tuples. *)
                        fun isRec([], _) = true
                        |   isRec({name, ...} :: l, n) = name = Int.toString n andalso isRec(l, n+1)
                        val isTuple = recordIsFrozen r andalso isRec(recList, 1) andalso List.length recList >= 2
                        val localLevel = level+1
                        val valToPrint = mkInd(0, arg1) and depthCode = mkInd(1, arg1)

                        fun asRecord([], _, _) = raise Empty (* Shouldn't happen. *)

                        |   asRecord([{name, typeof, ...}], offset, _) =
                            let
                                val entryCode =
                                    (* Last or only field: no separator. *)
                                    if offset = 0
                                    then (* optimised unary records *)
                                        mkEval(printCode(typeof, localLevel), [arg1])
                                    else mkEval(printCode(typeof, localLevel),
                                                [mkTuple[mkInd(offset, valToPrint), decDepth depthCode]])
                                val (start, terminator) =
                                    if isTuple then ([], ")")
                                    else ([codePrettyString(name^" ="), codePrettyBreak(1, 0)], "}")
                            in
                                codeList(start @ [entryCode, codePrettyString terminator], CodeZero)
                            end

                        |   asRecord({name, typeof, ...} :: fields, offset, depth) =
                            let
                                val (start, terminator) =
                                    if isTuple then ([], ")")
                                    else ([codePrettyString(name^" ="), codePrettyBreak(1, 0)], "}")
                            in
                                checkDepth(depthCode, depth,
                                    codeList(
                                        start @
                                        [
                                            mkEval(
                                                printCode(typeof, localLevel),
                                                [mkTuple[mkInd(offset, valToPrint), decDepth depthCode]]),
                                            codePrettyString ",",
                                            codePrettyBreak (1, 0)
                                        ],
                                        asRecord(fields, offset+1, depth+1)),
                                    codeList([codePrettyString ("..." ^ terminator)], CodeZero)
                                )
                            end
                    in
                        mkProc(
                            codePrettyBlock(1, false, [],
                                mkTuple[codePrettyString (if isTuple then "(" else "{"), asRecord(recList, 0, 0)]),
                            1, "print-labelled")
                    end

                |   FunctionType _ => mkProc(codePrettyString "fn", 1, "print-function")

                |   _ => mkProc(codePrettyString "<empty>", 1, "print-empty")
            )
    in
        printCode(ty, baseLevel)
    end

    and makeEq(ty, level, getTypeValueForID, typeVarMap): codetree =
    let
        fun equalityForConstruction(constr, args): codetree =
        (* Generate an equality function for a datatype construction. *)
        let
            (* Get argument types parameters for polytypes.  There's a special case
               here for type vars, essentially the type arguments to the datatype, to avoid taking
               apart the type value record and then building it again. *)
            fun getArg ty =
                if (case ty of TypeVar tyVar =>
                        (case tvValue tyVar of EmptyType => true | _ => false) | _ => false)
                then
                (
                    case findCachedTypeCode(typeVarMap, ty) of
                        SOME (code, _) => code level
                    |   NONE => raise InternalError "getArg"
                )
            else
                let
                    val eqFun = makeEq(ty, level, getTypeValueForID, typeVarMap)
                    open TypeValue
                in
                    (* We need a type value here.  The equality function will be used to compare
                       the argument type and the boxedness and size parameters may be needed for
                       the constructors. *)
                    createTypeValue{eqCode=eqFun, printCode=CodeZero,
                        boxedCode=boxednessForType(ty, level, getTypeValueForID, typeVarMap),
                        sizeCode=sizeForType(ty, level, getTypeValueForID, typeVarMap)}
                end

            val resFun =
            let
                val iden = tcIdentifier constr
            in
                (* Special case: If this is ref, Array.array or Array2.array we must use
                   pointer equality and not attempt to create equality functions for
                   the argument.  It may not be an equality type. *)
                if isPointerEqType iden
                then rtsFunction POLY_SYS_word_eq
                else
                let
                    open TypeValue
                    val codeForId =
                        extractEquality(getTypeValueForID(tcIdentifier constr, args, level))
                in
                    (* Apply the function we obtained to any type arguments. *)
                    if null args
                    then codeForId
                    else mkEval(codeForId, map getArg args)
                end
            end
        in
            resFun
        end
    in
        case ty of
            TypeVar tyVar =>
            (
                case tvValue tyVar of
                    OverloadSet _ =>
                         (* This seems to occur if there are what amount to indirect references to literals. *)
                        equalityForConstruction(typeConstrFromOverload(ty, false), [])

                |   EmptyType =>
                    (
                        case findCachedTypeCode(typeVarMap, ty) of
                            SOME (code, _) => TypeValue.extractEquality(code level)
                        |   NONE => raise InternalError "makeEq: should already have been handled"
                    )

                |   tyVal => makeEq(tyVal, level, getTypeValueForID, typeVarMap)
            )

        |   TypeConstruction{constr, args, ...} =>
                if tcIsAbbreviation constr  (* May be an alias *)
                then makeEq (makeEquivalent (constr, args), level, getTypeValueForID, typeVarMap)
                else equalityForConstruction(constr, args)

        |   LabelledType {recList=[{typeof=singleton, ...}], ...} =>
                (* Unary tuples are optimised - no indirection. *)
                makeEq(singleton, level, getTypeValueForID, typeVarMap)

        |   LabelledType {recList, ...} =>
            (* Combine the entries.
                fun eq(a,b) = #1 a = #1 b andalso #2 a = #2 b ... *)
            let
                (* Have to turn this into a new function. *)
                val newLevel = level+1
                fun combineEntries ([], _) = CodeTrue
                |   combineEntries ({typeof, ...} :: t, n) =
                    let
                        val compareElements =
                            makeEq(typeof, newLevel, getTypeValueForID, typeVarMap)
                    in
                        mkCand(
                            mkEval(compareElements, [mkInd(n, arg2), mkInd(n, arg1)]),
                            combineEntries (t, n+1))
                    end
                val tupleCode = combineEntries(recList, 0)
             in
                mkProc(tupleCode, 2, "eq{...}(2)")
            end

        |   _ => raise InternalError "Equality for function"
    end

    (* Create equality functions for a set of possibly mutually recursive datatypes. *)
    fun equalityForDatatypes(typeDataList, eqAddresses, baseLevel, typeVarMap): (int * codetree) list =
    let
        val typesAndAddresses = ListPair.zipEq(typeDataList, eqAddresses)

        fun equalityForDatatype(({typeConstr=TypeConstrSet(tyConstr, vConstrs), eqStatus, boxedCode, sizeCode, ...}, addr),
                                otherFns) =
        if eqStatus
        then
        let
            val argTypes = tcTypeVars tyConstr

            (* Argument type variables. *)
            val (localArgList, argTypeMap) =
                case argTypes of
                    [] => ([], typeVarMap)
                |   _ =>
                    let
                        val nArgs = List.length argTypes
                        (* Add the polymorphic variables after the ordinary ones. *)
                        val argAddrs = List.tabulate(nArgs, fn n => n-nArgs-2)
                        val args = List.map(fn addr => fn l => mkLoad(addr, l-(baseLevel+1))) argAddrs
                        val varToArgMap = ListPair.zipEq(argTypes, args)
                        val localArgList = List.map (fn addr => mkLoad(addr, 0)) argAddrs
                        val addrs = ref 1 (* Make local declarations for any type values. *)
                        fun mkAddr n = !addrs before (addrs := !addrs + n)
                    in
                        (localArgList, extendTypeVarMap(varToArgMap, mkAddr, baseLevel+1, typeVarMap))
                    end

            val nTypeVars = List.length argTypes

            (* If this is a reference to a datatype we're currently generating
               load that address otherwise fall back to the default. *)
            fun getEqFnForID(typeId, _, l) =
                if sameTypeId(typeId, tcIdentifier tyConstr) andalso null argTypes
                then (* Directly recursive. *)
                    TypeValue.createTypeValue{eqCode=mkLoad(0, l-baseLevel-1), printCode=CodeZero,
                                    boxedCode=boxedCode, sizeCode=sizeCode}
                else
                case List.find(fn({typeConstr=tc, ...}, _) => sameTypeId(tcIdentifier(tsConstr tc), typeId)) typesAndAddresses of
                    SOME({boxedCode, sizeCode, ...}, addr) =>  (* Mutually recursive. *)
                         TypeValue.createTypeValue{eqCode=mkLoad(addr, l-baseLevel), printCode=CodeZero,
                                                   boxedCode=boxedCode, sizeCode=sizeCode}
                |   NONE => codeId(typeId, l)

            (* Filter out the EnumForm constructors.  They arise
               in situations such as datatype t = A of int*int | B | C
               i.e. where we have only one non-nullary constructor
               and it is a tuple.  In this case we can deal with all
               the nullary constructors simply by testing whether
               the two arguments are the same.  We don't have to
               discriminate the individual cases. *)
            fun (*isEnum(Value{class=Constructor{nullary=true, ...}, access=Global code, ...}) =
            let
                open ADDRESS
            in
                (* If the value is a short integer then we can check
                   for equality using pointer equality. *)
                isShort(loadWord(toAddress(evalue code), 0w1))
            end
            | *)isEnum _ = false

            fun processConstrs [] =
                (* The last of the alternatives is false *) CodeZero

            |  processConstrs ((vConstr as Value{class, access, typeOf, ...}) ::rest) =
                if isEnum vConstr then processConstrs rest
                else
                let
                    fun addPolymorphism c =
                        if nTypeVars = 0 orelse justForEqualityTypes then c else mkEval(c, localArgList)
                    val newLevel = baseLevel+1 (* We have one function. *)
                    val base = codeAccess(access, newLevel)
                    open ValueConstructor
                    fun matches arg = mkEval(addPolymorphism(extractTest base), [arg])
                in
                    case class of
                        Constructor{nullary=true, ...} =>
                            mkIf(matches arg1, matches arg2, processConstrs rest)
                    |    _ => (* We have to unwrap the value. *)
                        let
                            (* Get the constructor argument given the result type.  We might
                               actually be able to take the argument type off directly but
                               there's some uncertainty about whether we use the same type
                               variables for the constructors as for the datatype. (This only
                               applies for polytypes). *)
                            val resType = constructorResult(typeOf, List.map TypeVar argTypes)

                            (* Code to extract the value. *)
                            fun destruct argNo =
                                mkEval(addPolymorphism(extractProjection(codeAccess(access, newLevel))),
                                    [mkLoad(argNo, 0)])

                            (* Test whether the values match. *)
                            val eqValue =
                                mkEval(
                                    makeEq(resType, newLevel, getEqFnForID, argTypeMap),
                                    [destruct ~1, destruct ~2])
                        in
                            (* We have equality if both values match
                               this constructor and the values within
                               the constructor match. *)
                            mkIf(matches arg1, mkCand(matches arg2, eqValue), processConstrs rest)
                        end
                end

            (* We previously only tested for bit-wise (pointer) equality if we had
               at least one "enum" constructor in which case the test would eliminate
               all the enum constructors.  I've now extended this to all cases where
               there is more than one constructor.  The idea is to speed up equality
               between identical data structures. *)
            val eqCode =
                case vConstrs of
                   [vcons] => (* Single constructor. *)
                       if isEnum vcons
                       then CodeTrue (* Return true here: processConstrs would return false. *)
                       else processConstrs vConstrs
                 |  _ => (* More than one constructor: should never be zero. *)
                        mkCor(mkTestptreq(arg1, arg2), processConstrs vConstrs)
        in
            if null argTypes
            then (addr, mkProc(eqCode, 2, "eq-" ^ tcName tyConstr ^ "(2)")) :: otherFns
            else (* Polymorphic.  Add an extra inline functions. *)
            let
                val nArgs = List.length argTypes
                (* Call the second function with the values to be compared and the base types. *)
                val polyArgs = List.tabulate(nArgs, fn i => mkLoad(~nArgs+i, 1))
            in
                (addr,
                    mkInlproc(
                        mkInlproc(
                            mkEval(mkLoad(addr+1, 2), polyArgs @ [arg1, arg2]), 2, "eq-" ^ tcName tyConstr ^ "(2)"),
                            nArgs, "eq-" ^ tcName tyConstr ^ "(2)(P)")) ::
                (addr+1,
                    mkProc(mkEnv(getCachedTypeValues argTypeMap, eqCode), 2+nTypeVars, "eq-" ^ tcName tyConstr ^ "()")) ::
                otherFns
            end
        end
        else (* Not an equality type.  This will not be called but it still needs to
                be a function to ensure it's valid inside mkMutualDecs. *)
            (addr, mkProc(CodeZero, 2, "no-eq")) :: otherFns
    in
        List.foldl equalityForDatatype [] typesAndAddresses
    end

    (* Create a printer function for a datatype when the datatype is declared.
       We don't have to treat mutually recursive datatypes specially because
       this is called after the type IDs have been created. *)
    fun printerForDatatype(TypeConstrSet(typeConstr, vConstrs), level, typeVarMap) =
    let
        val name = tcName typeConstr
        val argTypes = tcTypeVars typeConstr
        val argCode = mkInd(0, arg1)
        and depthCode = mkInd(1, arg1)

        val (localArgList, innerLevel, newTypeVarMap) =
            case argTypes of
                [] => ([], level+1, typeVarMap)
            |   _ =>
                let
                    val (varToArgMap, localArgList) = mkTcArgMap(argTypes, level+1)
                    val addrs = ref 1 (* Make local declarations for any type values. *)
                    fun mkAddr n = !addrs before (addrs := !addrs + n)
                in
                    (localArgList, level+2, extendTypeVarMap(varToArgMap, mkAddr, level+1, typeVarMap))
                end

        (* If we have an expression as the argument we parenthesise it unless it is
           a simple string, a tuple, a record or a list.
           A reference to this function is copied into the code.
           N.B.  This code is also in InitialBasis to handle "ref" and "option". *)
        fun parenthesise p =
            if isPrettyBlock p
            then
            let
                val (_, _, _, items) = projPrettyBlock p
            in
                case items of
                    [ _ ] => p (* Just one item.  No need for parentheses. *)
                |   [] => p (* Shouldn't generally happen. *)
                |   first :: _ =>
                    if isPrettyString first
                        andalso (case projPrettyString first of
                                    "(" => true | "{" => true | "[" => true | _ => false)
                    then p (* Already parenthesised. *)
                    else
                        PrettyBlock(3, true, [], [ PrettyString "(", PrettyBreak(0, 0), p, PrettyBreak(0, 0), PrettyString ")" ])
            end
            else p (* String or Break *)

        fun printerForConstructors
                (Value{name, typeOf, access, class = Constructor{nullary, ...}, ...} :: rest) =
            let
                (* The "value" for a value constructor is a tuple containing
                   the test code, the injection and the projection functions. *)
                val constructorCode = codeAccess(access, innerLevel)

                (* If this is a polytype the fields in the constructor tuple are functions that first
                   have to be applied to the type arguments to yield the actual injection/test/projection
                   functions.  For monotypes the fields contain the injection/test/projection
                   functions directly. *)
                fun addPolymorphism c =
                   if null argTypes  orelse justForEqualityTypes then c else mkEval(c, localArgList)

                open ValueConstructor

                val printCode =
                    if nullary
                    then (* Just the name *) codePrettyString name
                    else
                    let
                        val typeOfArg =
                            case typeOf of
                                FunctionType{arg, ...} => arg
                            |   _ => raise InternalError "contructor not a function"
                        val getValue = mkEval(addPolymorphism(extractProjection constructorCode), [argCode])
                        
                    in
                        codePrettyBlock(1, false, [],
                            codeList(
                                [
                                    codePrettyString name,
                                    codePrettyBreak (1, 0),
                                    (* Print the argument and parenthesise it if necessary. *)
                                    mkEval(mkConst(toMachineWord parenthesise),
                                        [
                                            mkEval(
                                                printerForType(typeOfArg, innerLevel, newTypeVarMap),
                                                [mkTuple[getValue, decDepth depthCode]]
                                            )]
                                        )
                                ], CodeZero))
                    end
            in
                (* If this was the last or only constructor we don't need to test. *)
                checkDepth(depthCode, 1,
                    if null rest
                    then printCode
                    else
                    let
                        val testValue = mkEval(addPolymorphism(extractTest constructorCode), [argCode])
                    in
                        mkIf(testValue, printCode, printerForConstructors rest)
                    end,
                    codePrettyString "...")
            end

        |   printerForConstructors _ = raise InternalError ("No constructors:"^name)
            
        val printerCode = printerForConstructors vConstrs
    in
        (* Wrap this in the functions for the base types. *)
        if null argTypes
        then mkProc(printerCode, 1, "print-"^name)
        else mkProc(mkEnv(getCachedTypeValues newTypeVarMap,
                            mkProc(printerCode, 1, "print-"^name)),
                    List.length argTypes, "print"^name^"()")
    end    

    (* Opaque matching and functor application create new type IDs using an existing
       type as implementation.  The equality function is inherited whether the type
       was specified as an eqtype or not.  The print function is inherited but a new
       ref is created so that if a pretty printer is installed for the new type it
       does not affect the old type. *)
    (* If this is a type function we're going to generate a new ref anyway so we
       don't need to copy it. *)
    fun codeGenerativeId(sourceId as TypeId{typeFn=(_, EmptyType), ...}, _, mkAddr, level) =
        let (* Datatype. *)
            open TypeValue
            val { dec, load } = multipleUses (codeId(sourceId, level), fn () => mkAddr 1, level)
            val loadLocal = load level
            val printCode =
                    mkEval
                        (rtsFunction POLY_SYS_alloc_store,
                        [mkConst (toMachineWord 1), mkConst (toMachineWord mutableFlags),
                         mkEval(rtsFunction POLY_SYS_load_word,
                            [extractPrinter loadLocal, CodeZero])
                          ])
        in
            mkEnv(
                dec,
                createTypeValue {
                    eqCode = extractEquality loadLocal, printCode = printCode,
                    boxedCode = extractBoxed loadLocal, sizeCode = extractSize loadLocal
                }
             )
        end

    |   codeGenerativeId(TypeId{typeFn=([], resType), ...}, isEq, mkAddr, level) =
        let (* Monotype abbreviation. *)
            (* Create a new type value cache. *)
            val typeVarMap = defaultTypeVarMap(mkAddr, level)

            open TypeValue
            (* Create a printer for a type function.  This is used to create the general
               print function for any type. *)
            val printCode = printerForType(resType, level, typeVarMap)

            val eqCode =
                if not isEq then CodeZero
                else (* We need a function that takes two arguments rather than a single pair. *)
                    makeEq(resType, level, fn (typeId, _, l) => codeId(typeId, l), typeVarMap)
            val boxedCode =
                boxednessForType(resType, level, fn (typeId, _, l) => codeId(typeId, l), typeVarMap)
            val sizeCode =
                sizeForType(resType, level, fn (typeId, _, l) => codeId(typeId, l), typeVarMap)
        in
            mkEnv(
                TypeVarMap.getCachedTypeValues typeVarMap,
                createTypeValue {
                    eqCode = eqCode, boxedCode = boxedCode, sizeCode = sizeCode,
                    printCode =
                    mkEval
                        (rtsFunction POLY_SYS_alloc_store,
                        [mkConst (toMachineWord 1), mkConst (toMachineWord mutableFlags),
                         printCode])
                })
        end

    |   codeGenerativeId(TypeId{typeFn=(argTypes, resType), ...}, isEq, mkAddr, level) =
        let (* Polytype abbreviation: All the entries in the tuple are functions that must
               be applied to the base type values when the type constructor is used. *)
            (* Create a new type value cache. *)
            val typeVarMap = defaultTypeVarMap(mkAddr, level)
            val nArgs = List.length argTypes

            open TypeValue
            (* Create a printer for a type function.  This is used to create the general
               print function for any type. *)
            val printCode =
                let
                    val addrs = ref 1 (* Make local declarations for any type values. *)
                    fun mkAddr n = !addrs before (addrs := !addrs + n)
                    val argTypeMap = extendTypeVarMap(#1(mkTcArgMap(argTypes, level+1)), mkAddr, level+1, typeVarMap)
                    val printCode = printerForType(resType, level+1, argTypeMap)
                in
                    (* Wrap this in a function for the type arguments. *)
                    mkProc(
                        mkEnv(getCachedTypeValues argTypeMap, printCode),
                        nArgs, "print-helper()")
                end

            val eqCode =
                if not isEq then CodeZero
                else
                    let
                        val addrs = ref 1 (* Make local declarations for any type values. *)
                        fun mkAddr n = !addrs before (addrs := !addrs + n)
                        val argTypeMap =
                            extendTypeVarMap(#1 (mkTcArgMap(argTypes, level+1)), mkAddr, level+1, typeVarMap)

                        val innerFnCode =
                            makeEq(resType, level+1, fn (typeId, _, l) => codeId(typeId, l), argTypeMap)
                    in
                        mkInlproc(mkEnv(getCachedTypeValues argTypeMap, innerFnCode), nArgs, "equality()")
                    end
            val boxedCode =
                let
                    val addrs = ref 1 (* Make local declarations for any type values. *)
                    fun mkAddr n = !addrs before (addrs := !addrs + n)
                    val argTypeMap =
                        extendTypeVarMap(#1 (mkTcArgMap(argTypes, level+1)), mkAddr, level+1, typeVarMap)
                    val innerFnCode =
                        boxednessForType(resType, level+1, fn (typeId, _, l) => codeId(typeId, l), argTypeMap)
                in
                    mkInlproc(mkEnv(getCachedTypeValues argTypeMap, innerFnCode), nArgs, "boxedness()")
                end
            val sizeCode =
                let
                    val addrs = ref 1 (* Make local declarations for any type values. *)
                    fun mkAddr n = !addrs before (addrs := !addrs + n)
                    val argTypeMap =
                        extendTypeVarMap(#1 (mkTcArgMap(argTypes, level+1)), mkAddr, level+1, typeVarMap)
                    val innerFnCode =
                        sizeForType(resType, level+1, fn (typeId, _, l) => codeId(typeId, l), argTypeMap)
                in
                    mkInlproc(mkEnv(getCachedTypeValues argTypeMap, innerFnCode), nArgs, "size()")
                end
        in
            mkEnv(
                TypeVarMap.getCachedTypeValues typeVarMap,
                createTypeValue {
                    eqCode = eqCode, boxedCode = boxedCode,
                    printCode =
                    mkEval
                        (rtsFunction POLY_SYS_alloc_store,
                        [mkConst (toMachineWord 1), mkConst (toMachineWord mutableFlags),
                         printCode]),
                    sizeCode = sizeCode
                })
        end


    (* Create the equality and type functions for a set of mutually recursive datatypes. *)
    fun createDatatypeFunctions(
            typeDatalist: {typeConstr: typeConstrSet, eqStatus: bool, boxedCode: codetree, sizeCode: codetree } list,
            mkAddr, level, typeVarMap) =
    let
        (* Each entry has an equality function and a ref to a print function.
           The print functions for each type needs to indirect through the refs
           when printing other types so that if a pretty printer is later
           installed for one of the types the others will use the new pretty
           printer.  That means that the code has to be produced in stages. *)
        (* Create the equality functions.  Because mutual decs can only be functions we
           can't create the typeIDs themselves as mutual declarations. *)
        local
            (* If this is polymorphic make two addresses, one for the returned equality function and
               one for the inner function. *)
            fun makeEqAddr{typeConstr=TypeConstrSet(tyConstr, _), ...} =
                mkAddr(if null (tcTypeVars tyConstr) then 1 else 2)
        in
            val eqAddresses = List.map makeEqAddr typeDatalist (* Make addresses for the equalities. *)
        end
        val equalityFunctions =
            mkMutualDecs(equalityForDatatypes(typeDatalist, eqAddresses, level, typeVarMap))

        (* Create the typeId values and set their addresses.  The print function is
           initially set as zero. *)
        local
            fun makeTypeId({typeConstr, boxedCode, sizeCode, ...}, eqAddr) =
            let
                val var = vaLocal(idAccess(tcIdentifier(tsConstr typeConstr)))
                val newAddr = mkAddr 1
                open TypeValue
                val idCode =
                    createTypeValue
                    {
                        eqCode=mkLoad(eqAddr, 0),
                        printCode=
                            mkEval
                                (rtsFunction POLY_SYS_alloc_store,
                                [mkConst (toMachineWord 1), mkConst (toMachineWord mutableFlags),
                                 CodeZero (* Temporary - replaced by setPrinter. *)]
                            ),
                        boxedCode = boxedCode,
                        sizeCode = sizeCode
                    }
            in
                #addr var := newAddr;
                #level var:= level;
                mkDec(newAddr, idCode)
            end
        in
            val typeIdCode = ListPair.map makeTypeId (typeDatalist, eqAddresses)
        end

        (* Create the print functions and set the printer code for each typeId. *)
        local
            fun setPrinter{typeConstr=tc, ...} =
                mkNullDec(
                    mkEval(
                        rtsFunction POLY_SYS_assign_word,
                        [TypeValue.extractPrinter(codeId(tcIdentifier(tsConstr tc), level)),
                                  CodeZero, printerForDatatype(tc, level, typeVarMap)]
                        ))
        in
            val printerCode = List.map setPrinter typeDatalist
        end
    in
        equalityFunctions :: typeIdCode @ printerCode
    end


    (* Exported function.  Returns a function from an ML pair of values to bool.
       N.B. This differs from the functions in the typeID which take a Poly pair. *)
    fun equalityForType(ty: types, level: int, typeVarMap: typeVarMap): codetree =
    let
        (* The final result function must take a single argument. *)
        val resultCode =
            makeEq(ty, level+1, fn (typeId, _, l) => codeId(typeId, l), typeVarMap)
    in
        (* We need to wrap this up in a new inline function. *)
        mkInlproc(mkEval(resultCode, [mkInd(0, arg1), mkInd(1, arg1)]),
                  1, "equality")
    end

    (* This code is used when the type checker has to construct a unique monotype
       because a type variable has escaped to the top level.
       The equality code always returns true and the printer prints "?". *)
    fun codeForUniqueId() =
    let
        open TypeValue
        fun monotypePrinter _ = PRETTY.PrettyString "?"
        val alwaysTrue = mkProc(CodeTrue, 2, "codeForUniqueId-equal")
    in
        createTypeValue{
            eqCode = alwaysTrue, printCode = mkConst (toMachineWord (ref monotypePrinter)),
            boxedCode = boxedEither, sizeCode = singleWord }
    end

    (* Use constants here rather than code-generating new functions each time.
       The equality function is actually wrong: it expects only a
       single argument when if it is ever called (it shouldn't be) it will
       be given a pair. *)
    val noEquality = mkConst (toMachineWord (fn _ => raise Fail "Not equality"))
    (* Since we don't have a way of writing a "printity" type variable there are cases
       when the printer will have to fall back to this. e.g. if we have a polymorphic
       printing function as a functor argument. *)
    val noPrinter = mkConst (toMachineWord (fn _ => PRETTY.PrettyString "?"))

    (* If this is a polymorphic value apply it to the type instance. *)
    fun applyToInstance'([], level, _, code) = code level (* Monomorphic. *)

    |   applyToInstance'(sourceTypes, level, polyVarMap, code) =
    let
        (* If we need either the equality or print function we generate a new
           entry and ignore anything in the cache. *)
        fun makePolyParameter {value=t, equality, printity} =
            if equality orelse printity
            then
                let
                    open TypeValue
                    fun getTypeValueForID(typeId, _, l) = codeId(typeId, l)
                    val eqCode =
                        if equality
                        then makeEq(t, level, fn (typeId, _, l) => codeId(typeId, l), polyVarMap)
                        else noEquality
                    val boxedCode = boxednessForType(t, level, getTypeValueForID, polyVarMap)
                    val printCode =
                        if printity then printerForType(t, level, polyVarMap) else noPrinter
                    val sizeCode = sizeForType(t, level, getTypeValueForID, polyVarMap)
                in
                    createTypeValue{
                        eqCode=eqCode, printCode=printCode,
                        boxedCode=boxedCode, sizeCode=sizeCode}
                end
            else (* If we don't require the equality or print function we can use the cache. *)
            case findCachedTypeCode(polyVarMap, t) of
                SOME (code, _) => code level
            |   NONE =>
                let
                    val maxCache = getMaxDepth polyVarMap (t, 1)
                    val cacheEntry = List.nth(polyVarMap, List.length polyVarMap - maxCache)
                    val { cache, mkAddr, level=decLevel, ...} = cacheEntry
                    local
                        open TypeValue
                        val boxedCode =
                            boxednessForType(t, decLevel, fn (typeId, _, l) => codeId(typeId, l), polyVarMap)
                        val sizeCode =
                            sizeForType(t, decLevel, fn (typeId, _, l) => codeId(typeId, l), polyVarMap)
                    in
                        val typeValue =
                            createTypeValue{
                                eqCode=noEquality, printCode=noPrinter,
                                boxedCode=boxedCode, sizeCode=sizeCode}
                    end
                    (* Make a new entry and put it in the cache. *)
                    val decAddr = mkAddr 1
                    val () = cache := {decCode = mkDec(decAddr, typeValue), typeOf = t, address = decAddr } :: !cache
                in
                    mkLoad(decAddr, level-decLevel)
                end
    in
        mkEval(code level, List.map makePolyParameter sourceTypes)
    end

    (* For now limit this to equality types. *)
    fun applyToInstance(sourceTypes, level, polyVarMap, code) =
        applyToInstance'(
            List.filter(fn {equality, ...} => not justForEqualityTypes orelse equality) sourceTypes,
            level, polyVarMap, code)

    structure Sharing =
    struct
        type typeId     = typeId
        type codetree   = codetree
        type types      = types
        type typeConstrs= typeConstrs
        type typeConstrSet=typeConstrSet
        type typeVarForm=typeVarForm
        type typeVarMap = typeVarMap
        type codeBinding    = codeBinding
    end
end;

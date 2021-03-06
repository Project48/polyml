(*
    Copyright (c) 2009 David C.J. Matthews

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

(*
    Title:      Operations on global and local values.
    Author:     Dave Matthews, Cambridge University Computer Laboratory
    Copyright   Cambridge University 1986
*)

functor DATATYPE_REP (

structure CODETREE : CODETREESIG
structure STRUCTVALS : STRUCTVALSIG;
structure TYPESTRUCT : TYPETREESIG

structure MISC :
sig
  exception InternalError of string; (* compiler error *)
  
  val quickSort : ('a -> 'a -> bool) -> 'a list -> 'a list
end;

structure ADDRESS :
sig
  type machineWord;      (* any legal bit-pattern (tag = 0 or 1) *)
  type address;   (* an object that's represented as a pointer *) 
  type short = Word.word;   (* an object that's represented as a 30-bit int *)
  
  val isShort   : 'a -> bool;

  (* The following casts are always safe *)
  val toMachineWord : 'a    -> machineWord;
  
  (* The following casts are checked at run-time *)
  val toShort   : 'a -> short;
  val toAddress : 'a -> address;
  
  val alloc:  (short * Word8.word * machineWord)  -> address;

  val loadWord:   (address * short) -> machineWord;
  val assignWord: (address * short * machineWord)  -> unit
  val lock:       address -> unit

  val length: address -> short;
   
  val F_words     : Word8.word;
  val F_bytes     : Word8.word;
  val F_mutable   : Word8.word;
end;

structure COPIER: COPIERSIG
structure TYPEIDCODE: TYPEIDCODESIG

sharing STRUCTVALS.Sharing = TYPESTRUCT.Sharing
      = COPIER.Sharing = CODETREE.Sharing = ADDRESS = MISC
      = TYPEIDCODE.Sharing

) : DATATYPEREPSIG =

struct
    open MISC; 

    open CODETREE;
    open TYPESTRUCT; (* Open this first because unitType is in STRUCTVALS as well. *)
    open Universal; (* for tag etc. *)
    open STRUCTVALS;
    open ADDRESS;
    open RuntimeCalls; (* for POLY_SYS and EXC numbers *)
    open TYPEIDCODE
    open COPIER

    val length   = List.length;

    val orb = Word8.orb
    infix 7 orb;

    (* These are the possible representations of a value constructor. *)
    datatype representations = 
        RefForm            (* As for OnlyOne but must be a monotype. *)
    |   UnitForm           (* If the only value in an enumeration. *)
    |   OnlyOne            (* If only one constructor, there is no tag or box. *)
                       (* Could be replaced by "UnboxedForm"? *)
    
    |   EnumForm of { tag: word, maxTag: word }    (* Enumeration - argument is the number. *)
    |   ShortForm of word  (* As EnumForm except that one datatype is BoxedForm. *)
    |   BoxedForm          (* Boxed but not tagged (only unary constructor) *)
    |   UnboxedForm        (* Unboxed and untagged (only unary constructor) *)

    |   ConstForm of { value: machineWord, maxTag: word}   (* Constant - argument is a tagged value. *)
    |   TaggedBox of { tag: word, maxTag: word }          (* Union - tagged and boxed.  i.e. the representation is a
                                 pair whose first word is the tag and second is the value. *)
    |   TaggedTuple of { tag: word, maxTag: word, size: int }
                                (* Union - tagged but with in-line tuple. i.e. for a
                                   tuple of size n the representation is a tuple of size n+1
                                 whose first word contains the tag. *)


    val arg1     = mkLoad (~1, 0); (* saves a lot of garbage *)

    val mutableFlags = F_words orb F_mutable;

    (* allocate 1 mutable word, initialise to "v"; do not evaluate "early" *)
    fun refApplyCode (v: codetree) =
      mkEval
        (rtsFunction POLY_SYS_alloc_store,
        [mkConst (toMachineWord 1), mkConst (toMachineWord mutableFlags), v]);

    local
        fun mkTag (tag:word) : codetree = mkConst (toMachineWord tag);

        (* How to apply the constructor at run-time or, if, it's a constant make it now. *)
        fun constrApply (test: representations, arg) : codetree =
        let
            fun tagTupleCode (tag:word, n:int, name:string) : codetree =
            let
                (* copy n data fields out of ordinary record *)
                fun getFields i =
                    if i < n then mkInd (i, arg1) :: getFields (i + 1) else [];
            in  
                mkInlproc (mkTuple (mkTag tag :: getFields 0), 1, name)
            end;

            fun tagTupleApplyCode (tag: word, n:int, arg:codetree) : codetree =
                mkEval (tagTupleCode(tag, n, ""), [arg]);
        in
            case test of
              UnboxedForm          => arg
            | BoxedForm            => mkTuple [arg]
            | RefForm              => refApplyCode arg
            | TaggedBox{tag, ...}  => mkTuple [mkTag tag, arg]
            | TaggedTuple{tag, size, ...} => tagTupleApplyCode(tag, size, arg)
            | OnlyOne              => arg
            | ConstForm{ value, ...} => mkConst value (* tagged value. *)
            | EnumForm{tag, ...}   => mkConst (toMachineWord tag)
            | ShortForm tag        => mkConst (toMachineWord tag)
            | UnitForm             => CodeZero
        end

        (* The run-time test whether a value matches a constructor. *)
        fun constrMatch (test: representations, value:codetree) : codetree =
        let
            fun testTag (tag, v) = mkTestinteq (mkTag tag, v)

            fun testTagOf(tag, maxTag, v) = mkTagTest (v, tag, maxTag)
      
            fun testBoxedTagOf (tag, maxTag, v) =
                testTagOf (tag, maxTag, mkInd (0, v)); (* tag is first field *)

            fun testBoxed (v: codetree) : codetree = (* not (isShort v) *)
                mkEval (rtsFunction POLY_SYS_not_bool,
                    [mkEval (rtsFunction POLY_SYS_is_short, [v])]);

            (* get the tag from a TaggedBox or ConstForm *)
            fun loadTag (u: machineWord) : machineWord = 
                loadWord (toAddress u, 0w0); (* tag is first field *)
        in
            case test of
                UnboxedForm          => testBoxed value
            |   BoxedForm            => testBoxed value
            |   RefForm              => CodeTrue
            |   EnumForm{tag, maxTag}   => testTagOf(tag, maxTag, value)
            |   ShortForm tag        => testTag(tag, value)
            |   TaggedBox{tag, maxTag}  => testBoxedTagOf(tag, maxTag, value)
            |   TaggedTuple{tag, maxTag, ...} => testBoxedTagOf(tag, maxTag, value)
            |   ConstForm{value=c, maxTag} => testBoxedTagOf(toShort (loadTag c), maxTag, value)
            |   OnlyOne              => CodeTrue
            |   UnitForm             => CodeTrue
        end

      (* The run-time code to destruct a construction. *)
      (* shouldn't the CodeZero's raise an exception instead?
         No, because I think there are circumstances in which the destructor
         code is created even for nullary constructors. *)
        fun constrDestruct (test: representations, value: codetree) : codetree =
        let
            fun taggedTupleDestructCode (n:int, arg:codetree) : codetree =
            let
                (* copy n data fields out of tagged record *)
                fun getFields i = if i <= n then mkInd (i, arg1) :: getFields (i + 1) else [];
    
                val proc = mkInlproc (mkTuple (getFields 1), 1, "")
            in  
                mkEval (proc, [arg])
            end;

            (* Use loadWord not indirect because the optimiser reorders indirections. *)
            fun refDestructCode (v : codetree) : codetree =
               mkEval  (rtsFunction POLY_SYS_load_word, [v, CodeZero])
        in
            case test of
                UnboxedForm          => value
            |   BoxedForm            => mkInd (0, value)
            |   RefForm              => refDestructCode value
            |   TaggedBox _         => mkInd (1, value) (* contents is second field of record *)
            |   TaggedTuple { size, ...} => taggedTupleDestructCode(size, value)
            |   OnlyOne              => value
            |   EnumForm _         => CodeZero (* To keep optimiser happy. *)
            |   ShortForm _         => CodeZero (* To keep optimiser happy. *)
            |   ConstForm _          => CodeZero (* (rather than raising an exception) *)
            |   UnitForm             => CodeZero
        end

        open ValueConstructor
    in
     (* Constructors are now represented as run-time values.  A nullary constructor is
        a pair consisting of a test function and the constructor value.  A unary
        constructor is a triple: a test function, an injection function and a
        projection function.
        The above applies to monotypes.  If this is a polytype each of these is
        actually a function from the base type values to the functions. *)

        fun createNullaryConstructor (test, tvs, name): codetree =
        let
            val numTypes =
                if justForEqualityTypes then 0 else List.length tvs
            val testFn = mkInlproc(constrMatch(test, arg1), 1, name) (* Test function. *)
            and constrVal = constrApply(test, CodeZero) (* Value. *)
        in
            if numTypes = 0
            then createNullaryConstr{ testMatch = testFn, constrValue = constrVal }
            else createNullaryConstr{
                    testMatch = mkInlproc(testFn, numTypes, name),
                    constrValue = mkInlproc(constrVal, numTypes, name)}
        end
    
        fun createUnaryConstructor(test: representations, tvs, name: string): codetree =
        let
            val numTypes =
                if justForEqualityTypes then 0 else List.length tvs
            val testMatch = mkInlproc(constrMatch(test, arg1), 1, name) (* Test function. *)
            and injectValue = mkInlproc(constrApply(test, arg1), 1, name) (* Injection function. *)
            and projectValue = mkInlproc(constrDestruct(test, arg1), 1, name) (* Projection function. *)
        in
            if numTypes = 0
            then createValueConstr{testMatch = testMatch, injectValue = injectValue, projectValue = projectValue }
            else createValueConstr{
                    testMatch = mkInlproc(testMatch, numTypes, name),
                    injectValue = mkInlproc(injectValue, numTypes, name),
                    projectValue = mkInlproc(projectValue, numTypes, name)}
        end
    end

(* 
     RefForm is used for "ref" (only). We use various representations
     of datatype / abstype constructors.
   
     Nullary constructors are represented as:
        UnitForm     (if it's the only constructor in the datatype)
        EnumForm     (if all the constructors are nullary)
        ShortForm     (if there's one unary constructor)
        ConstForm    (otherwise)
    
     Unary constructors are represented as:
        OnlyOne      (if it's the only constructor in the datatype)
        UnboxedForm  (if it's the only unary constructor, applied to a tuple)
        TaggedTuple  (if it's not the only unary constructor, applied to a tuple)
        BoxedForm    (if it's the only unary constructor)
        TaggedBox    (otherwise)
        
     Note that we can't use UnboxedForm/TaggedTuple if the datatype
     might appear in a signature, because the signature might be ambiguous
     about whether the argument is a tuple or not. In these cases, we always
     use BoxedForm/TaggedBox instead.
      
     Note that we use ConstForm, not EnumForm, for nullary constructors
     when the unary constructors are represented as TaggedTuple/TaggedBox
     because that allows the TaggedBox test to be:
       
         fn w => wordEq (loadWord (w,0), tag)
     
     rather than:
     
         fn w => not (isShort w) andalso wordEq (loadWord (w,0), tag)
     
     In my ignorance, I tried combining EnumForms with TaggedBoxes
     *without* changing the TaggedBox test and got a lot of
     core dumps as my reward (the machine didn't like fetching the
     tag from address 5!).
     
     SPF 20/10/94
  *)
  
  
  datatype constructorKind =
    Nullary             (* a nullary constructor *)
  | UnaryGeneric        (* a normal unary constructor *)
  | UnaryFunction       (* unary constructor applied to a function *)
  | UnaryTupled of int  (* a unary constructor applied to a tuple of size n  *)
  ;

    fun getTupleKind t =
      case t of
        (* We cannot have flexible records here.   All the fields must be listed. *)
        LabelledType {recList = [{typeof=t', ...}], ...} =>
            (* Singleton records are always represented simply by the value. *)
          getTupleKind t'
      
      | LabelledType {recList, ...} =>
          UnaryTupled (length recList)
      
      | FunctionType _ => 
          UnaryFunction
    
      | TypeConstruction {constr, args, ...} =>
        (
            (* We may have a type equivalence or this may be a datatype. *)
            if tcIsAbbreviation constr
            then getTupleKind (makeEquivalent(constr, args))
            else if sameTypeId (tcIdentifier constr, tcIdentifier refConstr)
            then UnaryGeneric (* A tuple ref is NOT the same as the tuple. *)
            else (* Datatype.  For the moment we only consider datatypes with a
                    single constructor since we want to find the width of the
                    tuple.  At present we simply return UnaryGeneric for all
                    other cases but it might be helpful to return a special
                    result when we have a datatype which we know will always
                    be boxed. *)
           (*     case tcConstructors constr of
                    [Value{typeOf, class=Constructor{nullary=false, ...}, ...}] =>
                        (* This may be a polymorphic datatype in which case
                           we have to invert the constructor to find the base type.
                           e.g. we may have an instance (int*int) t where t was
                           declared as datatype 'a t = X of 'a .*)
                        getTupleKind(constructorResult(typeOf, args))
                |    _ => UnaryGeneric
           *)  UnaryGeneric
        )
    
      | _ =>
          UnaryGeneric

    (* This now creates the functions as well as choosing the representation. *)
    (* N.B. The representation for the "context" and "pretty" datatypes is defined in Pretty.sml.
       Any changes here that may affect the representation of a datatype may require
       changes there as well. *)
    fun chooseConstrRepr(cs, tvs: types list) =
    let
        fun checkArgKind (name, EmptyType) = (Nullary, name)
        |   checkArgKind (name, argType) = (getTupleKind argType, name)
        val kinds = map checkArgKind cs;

        fun chooseRepr [(Nullary, name)]       = [createNullaryConstructor(UnitForm, tvs, name)]
        |  chooseRepr [(UnaryGeneric, name)]  = [createUnaryConstructor(OnlyOne, tvs, name)]
        |  chooseRepr [(UnaryFunction, name)] = [createUnaryConstructor(OnlyOne, tvs, name)]
        |  chooseRepr [(UnaryTupled _, name)] = [createUnaryConstructor(OnlyOne, tvs, name)]
        |  chooseRepr l =
            let
                val unaryCount = List.foldl(fn((Nullary, _), n) => n | (_,n) => n+1) 0 l
            in
                case unaryCount of
                    0 => (* All are nullary. *)
                    let
                        val maxTag = Word.fromInt(List.length l)-0w1 (* Largest no is length-1 *)
                        fun createRepr(_, []) = []
                        |   createRepr(n, (_, name) :: t) =
                                createNullaryConstructor(EnumForm{tag=n, maxTag=maxTag}, tvs, name) :: createRepr (n + 0w1, t)
                    in
                        createRepr(0w0, l)
                    end

                |   1 =>
                    let
                        (* We use this version if all the constructors are nullary (i.e. constants)
                           except one.  The unary constructor is represented
                           by the boxed value and the nullary constructors by untagged integers. *)
                        (* Note that "UnaryTupled 0" (which would arise as a result of a declaration of the
                           form  datatype t = A of () | ... ) can't be represented as "UnboxedForm"
                          because "{}" is represented as a short (unboxed) integer. *)
                        fun chooseOptimisedRepr1(_, _, []) = []

                        |   chooseOptimisedRepr1(n, tvs, (Nullary, name) :: t) =
                                 createNullaryConstructor(ShortForm n, tvs, name) :: chooseOptimisedRepr1 (n + 0w1, tvs, t)

                        |   chooseOptimisedRepr1(n, tvs, (UnaryGeneric, name) :: t) =
                                   createUnaryConstructor(BoxedForm, tvs, name) :: chooseOptimisedRepr1(n, tvs, t)

                        |   chooseOptimisedRepr1(n, tvs, (UnaryFunction, name) :: t) =
                                   createUnaryConstructor(UnboxedForm, tvs, name)  :: chooseOptimisedRepr1(n, tvs, t)

                        |   chooseOptimisedRepr1(n, tvs, (UnaryTupled 0, name) :: t) =
                                   createUnaryConstructor(BoxedForm, tvs, name)    :: chooseOptimisedRepr1(n, tvs, t)

                        |   chooseOptimisedRepr1(n, tvs, (UnaryTupled _, name) :: t) = 
                                   createUnaryConstructor(UnboxedForm, tvs, name)  :: chooseOptimisedRepr1(n, tvs, t)

                    in
                        chooseOptimisedRepr1(0w0, tvs, l) (* can save the box *)
                    end

                |   _ =>
                    let
                        (* We use this version there's more than 1 unary constructor. *)
                        (* With this representation constructors of small tuples make tuples of
                         size n+1 whose first word is the tag.  Nullary constructors are represented
                         by single word objects containing the tag. *)
                        val maxTag = Word.fromInt(List.length l) - 0w1 (* Largest no is length - 1 *)

                        fun chooseOptimisedRepr2(_, _, []) = []
                        |   chooseOptimisedRepr2(n, tvs, h :: t) = 
                            let
                                val repr = 
                                    case h of
                                        (Nullary,       name) =>
                                        let
                                            (* Make an object with the appropriate tag. Doing it here means we
                                             only do it once for this object. *)
                                            fun genConstForm (n :word) : representations =
                                            let
                                                val vec : address = alloc (0w1, F_words, toMachineWord n);
                                                val () = lock vec;
                                            in
                                                ConstForm{value=toMachineWord vec, maxTag=maxTag}
                                            end
                                        in
                                            createNullaryConstructor(genConstForm n, tvs, name)
                                        end
                                            
                                    |   (UnaryGeneric,  name) => createUnaryConstructor(TaggedBox{tag=n, maxTag=maxTag}, tvs, name)
                                    |   (UnaryFunction, name) => createUnaryConstructor(TaggedBox{tag=n, maxTag=maxTag}, tvs, name)
                                    |   (UnaryTupled i, name) =>
                                            createUnaryConstructor(
                                                if i <= 4 (*!maxPacking*)
                                                then TaggedTuple {tag=n, size=i, maxTag=maxTag}
                                                else TaggedBox{tag=n, maxTag=maxTag}, tvs, name)
                            in
                                repr :: chooseOptimisedRepr2(n + 0w1, tvs, t)
                            end;
                    in
                        chooseOptimisedRepr2(0w0, tvs, l) (* can use tagged tuples *)
                    end
            end;
        fun makeFun c = mkInlproc(c, List.length tvs, "boxed/size")
        val (boxed, size) =
            case tvs of
                [] => (* Monotype *) (TypeValue.boxedEither, TypeValue.singleWord)
            |   _ => (* Polytype *) (makeFun TypeValue.boxedEither, makeFun TypeValue.singleWord)
    in
        { constrs = chooseRepr kinds, boxed = boxed, size = size } 
    end;
 
   (* RefForm, NilForm and ConsForm are only used for built-in types *)


(*****************************************************************************)
(*             Standard values and exceptions.                               *)
(*****************************************************************************)

    (* Build a datatype within the basis. *)
    fun buildBasisDatatype(tcName, tIdPath, tyVars, isEqType: bool,
            mkValConstrs: typeConstrs -> (values * codetree) list * codetree * codetree) =
    let
        (* Create a temporary datatype.  The "name" we put in here is usually the
           same as the type constructor name except for datatypes in the PolyML structure
           which have the PolyML prefix. *)
        val description = basisDescription tIdPath
        val id =
            makeBoundId(Local{addr = ref 0, level = ref 0}, 0 (* IdNumber*),
                isEqType, true, description)
        val dtype = makeTypeConstructor (tcName, tyVars, id, [DeclaredAt inBasis]);
        (* Build the constructors. *)
        val (valConstrsAndDecs, boxedCode, sizeCode) = mkValConstrs dtype
        (* The constructors have to be ordered as in genValueConstrs in PARSE_TREE. *)
        fun leq (Value{name=xname, ...}, _) (Value{name=yname, ...}, _) = xname < yname;
        val sortedConstrs = quickSort leq valConstrsAndDecs;
        val initialTypeSet = TypeConstrSet(dtype, (List.map #1 valConstrsAndDecs))
        val addrs = ref 0
        fun mkAddrs n = ! addrs before (addrs := !addrs+n)

        fun declConstr(Value{access=Local{addr, level}, ...}, repr) =
            let
                val newAddr = mkAddrs 1
            in
                addr := newAddr;
                level := 0;
                (mkDec(newAddr, repr), mkLoad(newAddr, 0))
            end
        |   declConstr _ = raise InternalError "declConstr: not local"
        val (declConstrs, loadConstrs) = ListPair.unzip(List.map declConstr sortedConstrs)

        val defMap = TypeVarMap.defaultTypeVarMap(mkAddrs, 0)

        (* Create the datatype.  Sets the address of the local in "id". *)
        val dtCode =
            createDatatypeFunctions(
                [{typeConstr=initialTypeSet, eqStatus=isEqType, boxedCode=boxedCode, sizeCode=sizeCode}], mkAddrs, 0, defMap)

        (* Compile and execute the code to build the functions and extract the result. *)
        val globalCode =
            genCode(
                mkEnv(
                    declConstrs @ TypeVarMap.getCachedTypeValues defMap @ dtCode, mkTuple(codeId(id, 0) :: loadConstrs)),
                [])()
        val newId = makeFreeId(Global(mkInd(0, globalCode)), isEqType, description)
        
        (* Get the value constructors out as globals. *)
        fun mkGlobal((Value{name, typeOf, class, locations,  ...}, _), (decs, offset)) =
            (decs @ [Value{name=name, typeOf=typeOf, class=class, locations=locations, references=NONE,
                     instanceTypes=NONE, access=Global(mkInd(offset, globalCode))}], offset+1)
        val (gConstrs, _) = List.foldl mkGlobal ([], 1 (* Offset 0 is the type ID *)) sortedConstrs

        (* Finally copy the datatype to put in the code. *)
    in
        fullCopyDatatype(TypeConstrSet(dtype, gConstrs), fn 0 => newId | _ => raise Subscript, "")
    end

    (* Nil and :: are used in parsetree for lists constructed
       using [ ... ] and are also used for initialisation. *)
    local
        fun makeConsAndNil listType =
        let
            val listTypeVars  = tcTypeVars listType;
            val alpha         = TypeVar(hd listTypeVars);
            val alphaList     = mkTypeConstruction ("list", listType, [alpha], [DeclaredAt inBasis]);
            val consType      = mkFunctionType (mkProductType [alpha, alphaList], alphaList);
            val nilConstructor  =
                makeValueConstr ("nil", alphaList, true, 2, Local{addr=ref 0, level=ref 0}, [DeclaredAt inBasis])
            val consConstructor =
                makeValueConstr ("::",  consType,  false, 2, Local{addr=ref 0, level=ref 0}, [DeclaredAt inBasis])
            val nilRepresentation = createNullaryConstructor(EnumForm{tag=0w0, maxTag=0w0}, [alpha], "nil")
            val consRepresentation = createUnaryConstructor(UnboxedForm, [alpha], "::")
        in
            ([(nilConstructor, nilRepresentation), (consConstructor, consRepresentation)],
            mkInlproc(TypeValue.boxedEither, 1, "boxed-list"),
            mkInlproc(TypeValue.singleWord, 1, "size-list"))
        end
    in
        val listConstr =
            buildBasisDatatype("list", "list",
                [makeTv {value=EmptyType, level=generalisable, nonunifiable=false,
                         equality=false, printable=false}],
                  true, makeConsAndNil)
        val (nilConstructor, consConstructor) =
            case listConstr of
                TypeConstrSet(_, [consC as Value{name="::", ...}, nilC as Value{name="nil", ...}]) =>
                        (nilC, consC)
            |   _ => raise InternalError "nil and cons in wrong order"
    end

    local
        fun makeNoneAndSome optionType =
        let
            val optionTypeVars  = tcTypeVars optionType;
            val alpha         = TypeVar(hd optionTypeVars);
            val alphaOption   = mkTypeConstruction ("option", optionType, [alpha], [DeclaredAt inBasis]);
            val someType      = mkFunctionType (alpha, alphaOption);
            val noneConstructor  =
                makeValueConstr ("NONE", alphaOption, true, 2, Local{addr=ref 0, level=ref 0}, [DeclaredAt inBasis]);
            val someConstructor =
                makeValueConstr ("SOME", someType, false, 2, Local{addr=ref 0, level=ref 0}, [DeclaredAt inBasis]);
            val noneRepresentation = createNullaryConstructor(EnumForm{tag=0w0, maxTag=0w0}, [alpha], "NONE")
            and someRepresentation = createUnaryConstructor(BoxedForm, [alpha], "SOME")
        in
            ([(noneConstructor, noneRepresentation), (someConstructor, someRepresentation)],
             mkInlproc(TypeValue.boxedEither, 1, "boxed-option"),
             mkInlproc(TypeValue.singleWord, 1, "size-option"))
        end
    in
        val optionConstr=
            buildBasisDatatype("option", "option",
                [makeTv {value=EmptyType, level=generalisable, nonunifiable=false,
                         equality=false, printable=false}], true, makeNoneAndSome)
        val (noneConstructor, someConstructor) =
            case optionConstr of
                TypeConstrSet(_, [noneC as Value{name="NONE", ...}, someC as Value{name="SOME", ...}]) => (noneC, someC)
            |   _ => raise InternalError "NONE and SOME in wrong order"
    end

    local
        fun listConstruct (base : types) : types =
        let
            val TypeConstrSet(listCons, _) = listConstr
        in
            mkTypeConstruction ("list", listCons, [base], [DeclaredAt inBasis])
        end;

        val intTypeConstr    = TYPESTRUCT.intType
        val stringTypeConstr = TYPESTRUCT.stringType
        val boolTypeConstr   = TYPESTRUCT.boolType
    in
        local
            val fields =
                [
                    mkLabelEntry("file", stringTypeConstr), mkLabelEntry("startLine", intTypeConstr),
                    mkLabelEntry("startPosition", intTypeConstr), mkLabelEntry("endLine", intTypeConstr),
                    mkLabelEntry("endPosition", intTypeConstr)
                ]
        in
            val locationCons =
                makeTypeConstructor(
                    "location", [],
                    makeTypeFunction(basisDescription "location", ([], mkLabelled(sortLabels fields, true))),
                    [DeclaredAt inBasis])
            val locationConstr = TypeConstrSet(locationCons, [])
        end

        local
            (* Pretty print context information. *)
            fun makeConstructors typeconstr =
            let
                val contextType = mkTypeConstruction ("context", typeconstr, [], [DeclaredAt inBasis])
                val locationType = mkTypeConstruction ("location", locationCons, [], [DeclaredAt inBasis])
                val constrs =
                   [ ("ContextLocation", locationType),
                     ("ContextProperty", mkProductType[stringTypeConstr, stringTypeConstr])];
                (* The representation of this datatype is given in Pretty.sml and must be the same
                   as the representation that chooseConstrRepr will use. *)
                val numConstrs = List.length constrs
                fun makeCons(s,t) =
                    makeValueConstr(s, mkFunctionType(t, contextType), false, numConstrs,
                        Local{addr=ref 0, level=ref 0}, [DeclaredAt inBasis])
                val {constrs=constrCode, boxed, size} = chooseConstrRepr(constrs, [])
            in
                (ListPair.zipEq(List.map makeCons constrs, constrCode), boxed, size)
            end
        in
            val contextConstr =
                buildBasisDatatype("context", "PolyML.context", [], false, makeConstructors)
        end

        local
            fun makeConstructors typeconstr =
            let
                val TypeConstrSet(contextCons, _) = contextConstr
                val prettyType = mkTypeConstruction ("pretty", typeconstr, [], [DeclaredAt inBasis])
                val contextType = mkTypeConstruction ("context", contextCons, [], [DeclaredAt inBasis])

                val constrs =
                   [ ("PrettyBlock",
                        mkProductType[intTypeConstr, boolTypeConstr,
                                      listConstruct contextType, listConstruct prettyType]),
                     ("PrettyBreak", mkProductType[intTypeConstr, intTypeConstr]),
                     ("PrettyString", stringTypeConstr)];
                (* The representation of this datatype is given in Pretty.sml and must be the same
                   as the representation that chooseConstrRepr will use. *)
                val numConstrs = List.length constrs
                fun makeCons(s,t) =
                    makeValueConstr(s, mkFunctionType(t, prettyType), false, numConstrs,
                        Local{addr=ref 0, level=ref 0}, [DeclaredAt inBasis])
                val {constrs=constrCode, ...} = chooseConstrRepr(constrs, [])
            in
                (ListPair.zipEq(List.map makeCons constrs, constrCode), TypeValue.boxedEither, TypeValue.singleWord)
            end
        in
            val prettyConstr =
                buildBasisDatatype("pretty", "PolyML.pretty", [], false, makeConstructors)
        end
    end

    (* The representation of ptProperties is given in ExportTree.sml and must also match. *)

    (* Construct an exception identifier - This is a ref (so we can uniquely
       identify it) containing a print function for the type. *)
    local
        (* The exception identifier contains a value of type (exn*int->pretty) option. *)
        val TypeConstrSet(optionCons, _) = optionConstr
        and TypeConstrSet(prettyCons, _) = prettyConstr
        val exnPrinter =
            mkTypeConstruction ("option", optionCons,
                [
                    mkFunctionType(
                        mkProductType[TYPESTRUCT.exnType, TYPESTRUCT.intType],
                        mkTypeConstruction ("pretty", prettyCons, [], [DeclaredAt inBasis])
                    )
                ],
                [DeclaredAt inBasis])
    in
        fun mkExIden(ty, level, tvMap) =
        let
            (* Get the constructor tuple, select the constructor operation, apply it to the type. *)
            val makeSome =
                applyToInstance(
                    [{value=exnPrinter, equality=false, printity=false}], level, tvMap,
                        fn _ => mkInd(1, case someConstructor of Value { access, ...} => vaGlobal access))
            val makeNone =
                applyToInstance(
                    [{value=exnPrinter, equality=false, printity=false}], level, tvMap,
                        fn _ => mkInd(1, case noneConstructor of Value { access, ...} => vaGlobal access))
            val printerCode =
                case ty of
                    FunctionType { arg, ...} =>
                        mkEval(makeSome, [printerForType(arg, level, tvMap)])
                |   _ => makeNone
        in
            refApplyCode printerCode
        end
    end

    (* Types that can be shared. *)
    structure Sharing =
    struct
        type codetree       = codetree
        type types          = types
        type values         = values
        type typeConstrSet  = typeConstrSet
        type typeId         = typeId
        type typeVarForm    = typeVarForm
        type typeVarMap     = typeVarMap
    end

end;




(*
    Copyright (c) 2000
        Cambridge University Technical Services Limited

    Further development:
    Copyright (c) 2000-9 David C.J. Matthews

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
(*****************************************************************************)
(*                  PARSETREE export signature                               *)
(*****************************************************************************)

signature PARSETREESIG =
sig
    type machineWord;
    type types;
    type fixStatus;
    type lexan;
    type pretty;
    type typeId;
    type codetree;
    type env;
    type typeConstrs;
    type typeVarForm
    type values;
    type structVals;
    type environEntry;
    type structureIdentForm;
    type typeParsetree
    type funpattern
  
    type location =
        { file: string, startLine: int, startPosition: int, endLine: int, endPosition: int }
    type ptProperties
    type exportTree = location * ptProperties list
    type navigation =
        {parent: (unit -> exportTree) option,
         next: (unit -> exportTree) option,
         previous: (unit -> exportTree) option}
    type typeIdDescription = { location: location, name: string, description: string }

  (* An identifier is just a name. In the second pass it is associated
     with a particular declaration and the type is assigned into the
     type field. The type of this identifier is needed to deal with
     overloaded operators. If we have an occurence of ``='', say, the
     type of the value will be 'a * 'a -> bool but the type of a particular
     occurence, i.e. the type of the identifier must be int * int -> bool,
     say, after all the unification has been done. *)
          
  type parsetree and valbind and fvalbind and fvalclause and typebind
  and datatypebind and exbind and labelRecEntry; 
   
  val isIdent : parsetree -> bool;

  val mkIdent  : string * location -> parsetree; 
  val mkString : string * location -> parsetree;
  val mkInt    : string * location -> parsetree;
  val mkReal   : string * location -> parsetree;
  val mkChar   : string * location -> parsetree; 
  val mkWord   : string * location -> parsetree; 
  val mkApplic : parsetree * parsetree * location * bool -> parsetree;
  
  val mkCond   : parsetree * parsetree * parsetree * location -> parsetree;
  val mkTupleTree : parsetree list * location -> parsetree;
  
  val mkValDeclaration : 
       valbind list * 
       {
         lookup: string -> typeVarForm option,
         apply: (string * typeVarForm -> unit) -> unit
       } *
       {
         lookup: string -> typeVarForm option,
         apply: (string * typeVarForm -> unit) -> unit
       } * location ->  parsetree;
  
  val mkFunDeclaration : 
       fvalbind list *
       {
         lookup: string -> typeVarForm option,
         apply: (string * typeVarForm -> unit) -> unit
       } *
       {
         lookup: string -> typeVarForm option,
         apply: (string * typeVarForm -> unit) -> unit
       } * location ->  parsetree;
    
  val mkOpenTree : structureIdentForm list * location -> parsetree;
  val mkStructureIdent : string * location -> structureIdentForm;
  val mkValBinding : parsetree * parsetree * location -> valbind; 
  val recValbind: valbind;
  val mkClausal : fvalclause list * location -> fvalbind;
  val mkClause : funpattern * parsetree * location -> fvalclause;
  val mkFunPattern: parsetree * lexan -> funpattern * string * int
  val mkList : parsetree list * location -> parsetree;
  val mkConstraint : parsetree * typeParsetree * location -> parsetree; 
  val mkLayered : parsetree * parsetree * location -> parsetree; 
  val mkFn : parsetree list * location -> parsetree;
  val mkMatchTree : parsetree * parsetree * location -> parsetree; 
  val mkLocalDeclaration : parsetree list * parsetree list * location * bool -> parsetree;
  val mkTypeDeclaration : typebind list * location -> parsetree;
  val mkDatatypeDeclaration : datatypebind list * typebind list * location -> parsetree;
  val mkDatatypeReplication :
        { newType: string, oldType: string,
          newLoc: location, oldLoc: location, location: location } -> parsetree;
  val mkAbstypeDeclaration :
      datatypebind list * typebind list * parsetree list * location -> parsetree;
  val mkTypeBinding : string * typeVarForm list * typeParsetree option * bool * location * location -> typebind;
  val mkDatatypeBinding :
    string * typeVarForm list *
        {constrName: string, constrArg: typeParsetree option, idLocn: location} list * location * location -> datatypebind;
  val mkExBinding : string * parsetree * typeParsetree option * location * location -> exbind;
  val mkLabelledTree : labelRecEntry list * bool * location -> parsetree;
  val mkLabelRecEntry: string * location * parsetree * location -> labelRecEntry
  val mkSelector : string * location -> parsetree;
  val mkRaise : parsetree * location -> parsetree;
  val mkHandleTree : parsetree * parsetree list * location -> parsetree; 
  val mkWhile : parsetree * parsetree * location -> parsetree;
  val mkCase : parsetree * parsetree list * location -> parsetree;
  val mkAndalso : parsetree * parsetree * location -> parsetree;
  val mkOrelse : parsetree * parsetree * location -> parsetree;
  val mkDirective : string list * fixStatus * location -> parsetree; 
  val mkExpseq : parsetree list * location -> parsetree;
  val mkExDeclaration  : exbind list * location -> parsetree;
  val mkParenthesised: parsetree * location -> parsetree
  val unit      : location -> parsetree;
  val wildCard  : location -> parsetree;
  val emptyTree : parsetree;

    val pass2: parsetree * (bool * bool * typeIdDescription -> typeId) * env * lexan -> types

    type debugenv = environEntry list * (int->codetree)

    val gencode: parsetree * lexan * debugenv * int * int ref * string -> codetree list * debugenv;

    (* only used for debugging and error messages! *)
    val ptDisplay: parsetree * int -> pretty;

    val getExportTree: navigation * parsetree -> exportTree


    (* Types that can be shared. *)
    structure Sharing:
    sig
        type lexan      = lexan
        and  pretty     = pretty
        and  environEntry = environEntry
        and  codetree   = codetree
        and  types      = types
        and  values     = values
        and  typeId     = typeId
        and  structVals = structVals
        and  typeConstrs= typeConstrs
        and  typeVarForm=typeVarForm
        and  env        = env
        and  fixStatus  = fixStatus
        and  structureIdentForm = structureIdentForm
        and  typeParsetree = typeParsetree
        and  parsetree  = parsetree
        and  valbind    = valbind
        and  fvalbind   = fvalbind
        and  fvalclause = fvalclause
        and  typebind   = typebind
        and  datatypebind=datatypebind
        and  exbind     = exbind
        and  labelRecEntry=labelRecEntry
        and  ptProperties = ptProperties
    end
end (* PARSETREE export signature *);

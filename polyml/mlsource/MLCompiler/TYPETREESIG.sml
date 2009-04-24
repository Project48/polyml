(*
    Copyright (c) 2000
        Cambridge University Technical Services Limited
        
    Modified David C. J. Matthews 2009.

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
(*                  TYPETREE exports signature                               *)
(*****************************************************************************)
signature TYPETREESIG =
sig
    type types;
    type values;
    type typeConstrs;
    type lexan;
    type typeId;
    type pretty;
    type exportTree
    type navigation
    type location =
        { file: string, startLine: int, startPosition: int, endLine: int, endPosition: int }
    type matchResult = (types * types * string) option
    type locationProp
    type structVals

    val mkTypeVar:          int * bool * bool * bool -> types;
    val mkTypeConstruction: string * typeConstrs * types list * locationProp list -> types;
    val mkProductType:      types list -> types;
    val mkFunctionType:     types * types -> types;
    val mkLabelled:         {name: string, typeof: types } list * bool -> types;
    val mkLabelEntry:       string * types -> {name: string, typeof: types };
    val mkOverloadSet:      typeConstrs list -> types;
    val sortLabels:         {name: string, typeof: types } list -> {name: string, typeof: types } list;
    val entryNumber:        string * types -> int;
    val recordNotFrozen:    types -> bool;
    val recordWidth:        types -> int;
    val makeEquivalent:     typeConstrs * types list -> types;
    val firstArg:           types -> types;

    (* Follow a chain of unified type variables *)
    val eventual:           types -> types

    (* Test for function type and return function argument. *)
    val getFnArgType:   types -> types option
   
    (* Unify two type variables which would otherwise be non-unifiable. *)
    val linkTypeVars: types * types -> unit;
    val setTvarLevel: types * int -> unit;

    (* Fill in the values of type variables and make checks. *)
    val assignTypes: types * (string * location -> typeConstrs) * lexan -> unit;

    (* Copy a type. *)
    val copyType: types * (types -> types) * (typeConstrs -> typeConstrs) -> types;

    (* Print it out prettily *)
    val display: types * int *
        { lookupType: string -> typeConstrs option,
          lookupStruct: string -> structVals option} option -> pretty;

    (* Print out a type constructor. *)
    val displayTypeConstrs: typeConstrs * int -> pretty;

    (* A list of type variables. *)
    val displayTypeVariables: types list * int -> pretty list;

    (* Create an instance of an overloaded type. *)
    val generaliseOverload: types * typeConstrs list * bool -> types;

    (* Returns the preferred type constructor from an overload. *)
    val typeConstrFromOverload: types * bool -> typeConstrs;

    (* Error message when overloading cannot be resolved. It is put in this
     module because we want the message to refer to the argument. *)
    val overloadError: types * string * string * lexan * location -> unit;

    (* Check a set of mutually recursive datatypes to see which admit equality. *)
    val computeDatatypeEqualities: typeConstrs list -> unit;

    (* Unify two type structures to give a unified type. *)
    val unifyTypes: types * types -> matchResult

    (* Used to link a type constructor to a type as the result of a "where type"
       construction. *)
    val setWhereType: typeConstrs * types list * types * (string -> unit) -> unit;

    (* Check that a type constructor permits equality. *)
    val permitsEquality: typeConstrs -> bool;

    (* Generate new copies of all unbound type variables - this is used on all
       non-local values or constructors so that, for example, each occurence of
       "hd", which has type 'a list -> 'a, can be separately bound to types. *)
    val generalise: types -> types;

    (* Release type variables at this nesting level. *)
    val allowGeneralisation: types * int * bool *
                             lexan * location * (unit -> pretty) -> unit;

    (* Check for a local datatype "escaping".  Added for ML97. *)
    val checkForLocalDatatypes: types * int * (string -> unit) -> unit;

    (* Check for free type variables.  Added for ML97. *)
    val checkForFreeTypeVariables: string * types * lexan -> unit;

    val constructorResult: types * types list -> types;

    val findValueConstructor: values -> values;

    val copyTypeConstr:
        typeConstrs * (int -> bool) * (int -> typeId) * (types -> types) * string -> typeConstrs;

    val setTypeConstr: typeConstrs * (bool -> typeId) -> unit;

    val identical:       types * types -> bool;
    val identicalConstr: typeConstrs * typeConstrs -> bool;
    val identicalList:   types list * types list -> bool;

    val boolType:   types;
    val intType:    types;
    val charType:   types;
    val stringType: types;
    val realType:   types;
    val unitType:   types;
    val exnType:    types;
    val wordType:   types;
  
    val badType:    types;

    val isUndefinedTypeConstr: typeConstrs -> bool
    val isBadType:  types -> bool

    val sameTypeVar : types * types -> bool;

    (* If this is simply giving a new name to a type constructor returns the
       type identifier of the constructor that is being rebound. *)
    val typeNameRebinding: types list * types -> typeId option

    (* Parse tree operations. *)
    type typeParsetree
    val ParseTypeBad: typeParsetree
    val makeParseTypeConstruction:
        (string * location) * (typeParsetree list * location) * location -> typeParsetree
    val makeParseTypeProduct: typeParsetree list * location -> typeParsetree
    val makeParseTypeFunction: typeParsetree * typeParsetree * location -> typeParsetree
    val makeParseTypeLabelled:
        ((string * location) * typeParsetree * location) list * bool * location -> typeParsetree
    val makeParseTypeId: types * location -> typeParsetree
    val unitTree: location -> typeParsetree

    val typeFromTypeParse: typeParsetree -> types

    val typeExportTree: navigation * typeParsetree -> exportTree

end;


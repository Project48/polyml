(*
    Copyright (c) 2000
        Cambridge University Technical Services Limited

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
    Title:      Auxiliary Parsing Functions.
    Author:     Dave Matthews, Cambridge University Computer Laboratory
    Copyright   Cambridge University 1985
*)

functor SKIPS_ (

(*****************************************************************************)
(*                  SYMBOLS                                                  *)
(*****************************************************************************)
structure SYMBOLS :
sig
  type sys;
    
  val stringConst:  sys;
  val integerConst: sys;
  val eq :          sys * sys -> bool;
  val repr:         sys -> string;
end;

(*****************************************************************************)
(*                  LEX                                                      *)
(*****************************************************************************)
structure LEX :
sig
    type lexan;
    type sys;
    type location =
        { file: string, startLine: int, startPosition: int, endLine: int, endPosition: int }

    val sy:           lexan -> sys;
    val id:           lexan -> string;
    val insymbol:     lexan -> unit;
    val location:     lexan -> location;
    val errorMessage: lexan * location * string -> unit;
    val nullLocation: location
    val locSpan: location * location -> location
end;


(*****************************************************************************)
(*                  SYMSET                                                   *)
(*****************************************************************************)
structure SYMSET :
sig
  type symset;
  type sys;

  val inside:           sys * symset -> bool;
  val ++ :              symset * symset -> symset;
  val ident:            symset;
  val typeIdent:        symset;
  val integerConst:     symset;
  val realConst:        symset;
  val declarableVarSys: symset;
end;

(*****************************************************************************)
(*                  SKIPS sharing constraints                                *)
(*****************************************************************************)

sharing type
  SYMBOLS.sys
= LEX.sys
= SYMSET.sys

) : 

(*****************************************************************************)
(*                  SKIPS export signature                                   *)
(*****************************************************************************)
sig
  type sys;
  type lexan;
  type symset;
  type location =
        { file: string, startLine: int, startPosition: int, endLine: int, endPosition: int }
    
  val notfound: string * lexan -> unit;
  val badsyms:  sys * lexan -> unit;
  val getsym:   sys * lexan -> unit;
  val skipon:   symset * symset * string * lexan -> unit;
  val testfor:  sys * symset * lexan -> bool;
  val getid:    symset * symset * lexan -> string * location;
  val getLabel: symset * lexan -> string * location;
  val getList:  sys * symset * lexan * (unit -> 'a * location) -> 'a list * location;
end =

(*****************************************************************************)
(*                  SKIPS functor body                                       *)
(*****************************************************************************)
struct
  infix 5 inside;
  infix 6 ++;
  infix 8 eq;

  open SYMBOLS LEX SYMSET;
        
  local
    (* Result is in ``id'' *)    
    val idSys = ident ++ typeIdent ++ integerConst ++ realConst;
  in
    (* Try to recreate the original lexical item. *)
    fun notfound (expected, lex) = 
    let
      val found = 
        if sy lex eq SYMBOLS.stringConst 
          then "\"" ^ id lex ^ "\""
        else if sy lex inside idSys 
              then id lex
        else repr (sy lex)
    in
      errorMessage (lex, location lex,
         expected ^ " expected but " ^ found ^ " was found")
    end;
  end;

  fun badsyms (sym, lex) = 
    (notfound (repr sym, lex); insymbol lex);

  fun getsym (sym, lex) = 
     if (sy lex) eq sym then insymbol lex else notfound (repr sym, lex);

  infix 5 notin;
  
  fun a notin b = not (a inside b);

  (* used at the end of a syntactic unit to check that the current symbol
      is a suitable terminator *)
  fun skipon (a, b, errmss, lex) = 
    if (sy lex) notin a
    then
      (
        notfound (errmss, lex);
        while sy lex notin (a ++ b) do insymbol lex
      )
    else ();

  (* returns an identifier *)
    fun getid (syms, fsys, lex) = 
    if (sy lex) inside syms
    then
    let
        val iden = id lex
        val loc = location lex
    in
        insymbol lex;
        (iden, loc)
    end
    else
      (
        notfound ("Identifier", lex);
        while sy lex notin fsys do insymbol lex;
        ("", nullLocation)
      );

  (* Read a label and check that it is valid if numeric. *)
    fun getLabel (fsys, lex) = 
    if (sy lex) eq SYMBOLS.integerConst
    then
    let
        val iden = id lex;
        val loc = location lex
        val firstCh = String.str(String.sub(iden, 0));
    in
        insymbol lex;
        if firstCh = "~" orelse firstCh = "0"
        then errorMessage (lex, location lex, "Labels must be 1,2,3,....")
        else ();
        (iden, loc)
    end
    else getid (declarableVarSys, fsys, lex);
  
  (* Tests for a symbol and removes it if it is there.
     startsys is the set of symbols which can start each element. *)
  fun testfor (sym, startsys, lex) =
      (* repeat if the separator or a starting sym is found *)
      if sy lex eq sym
        then (insymbol lex; true)
      else if sy lex inside startsys
        then (badsyms (sym, lex); true)
      else false;

    fun getList (separator, startsys, lex, each) =
    let
        fun forList(list, startLoc) =
        let
            val (item, itemLoc) = each()
        in
            (* Add each item to the list.  The final span is from
               the start to the final location. *)
            if testfor (separator, startsys, lex)
            then forList(list @ [item], startLoc)
            else (list @ [item], locSpan(startLoc, itemLoc))
        end
     in
        forList ([], location lex)
    end;
end (* SKIPS *);

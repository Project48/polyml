(*
    Title:      Bootstrap code.
    Author:     David Matthews
    Copyright   David Matthews 2005-2008

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
RunCall.addOverload Bootstrap.convString Bootstrap.convStringName;
RunCall.addOverload Bootstrap.convInt "convInt";
RunCall.addOverload Bootstrap.convWord "convWord";

(* Build the main basis library. *)
Bootstrap.use "basis/build.sml";

(* We've now set up the new name space so everything has to be
   compiled into that rather than the old space. *)

let
    (* Bootstrap.use adds the path given as -I path but PolyML.make and PolyML.use
       don't.  Add the path explicitly. *)
    val args = CommandLine.arguments();
    fun getPath [] = "." (* Default path *)
      | getPath ("-I" :: outFile :: _) = outFile
      | getPath (_::tl) = getPath tl
    val path = getPath args
in
    (* FFI. *)
    PolyML.make (OS.Path.concat(path, "mlsource/extra/CInterface"));
    PolyML.use (OS.Path.concat(path, "mlsource/extra/CInterface/clean"));

    (* XWindows/Motif *)
    let
       val xcall: int*int->int*int =
        RunCall.run_call1 RuntimeCalls.POLY_SYS_XWindows;
       (* See if the RTS supports the X GetTimeOfDay call. *)
       val isX = (xcall(30, 0); true) handle _ => false
    in
       if isX
       then
        (
        PolyML.make "mlsource/extra/XWindows";
        PolyML.make "mlsource/extra/Motif"
        )
       else ()
    end
end;

PolyML.print_depth 10;

(* Set the inline level to 40 which seems optimal. *)
PolyML.Compiler.maxInlineSize := 40;

(* Write out the result as an export file. *)
let
    val args = CommandLine.arguments();
    (* If we have -o filename use that as the output name.
       N.B.  polyImport takes the first argument that is not recognised as
       an RTS argument and treats that as the file name so any -o must occur
       AFTER the import file. *)
    fun getFile [] = "polyexport" (* Default file name *)
      | getFile ("-o" :: outFile :: _) = outFile
      | getFile (_::tl) = getFile tl
    val fileName = getFile args
in
    PolyML.shareCommonData PolyML.rootFunction;
    PolyML.export(fileName, PolyML.rootFunction)
end;


(*
    Title:      Standard Basis Library: IntInf structure and signature.
    Author:     David Matthews
    Copyright   David Matthews 2000

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

(* G&R 2004 status: checked, no change. *)

signature INT_INF =
sig
    include INTEGER
    val divMod : int * int -> int * int
    val quotRem : int * int -> int * int
    val pow : int * Int.int -> int
    val log2 : int -> Int.int
    val orb : int * int -> int
    val xorb : int * int -> int
    val andb : int * int -> int
    val notb : int -> int
    val << : int * Word.word -> int
    val ~>> : int * Word.word -> int
end;

structure IntInf : INT_INF =
struct
    open Int (* Inherit everything from Int. *)
    open RuntimeCalls;
    
    val quotRem: int*int->int*int = RunCall.run_call2 POLY_SYS_quotrem

    (* This should really be defined in terms of quotRem. *)
    fun divMod(i, j) = (i div j, i mod j)

    (* Return the position of the highest bit set in the value. *)
    local
        val isShort: int -> bool = RunCall.run_call1 POLY_SYS_is_short
        val loadByte: int*int->word = RunCall.run_call2 POLY_SYS_load_byte
        val segLength: int -> int = RunCall.run_call1 POLY_SYS_get_length

        (* Compute log2 for a short value.  The top bit of i will always be
           zero since we've checked that it's positive so it will always
           terminate. *)
        fun log2Word(i: word, j: word, n: int) =
            if Word.>(j, i) then n-1
            else log2Word(i, Word.<<(j, 0w1), n+1)

        (* The value is represented as little-endian byte segment.
           High-order bytes may be zero so we work back until we
           find a non-zero byte and then find the bit-position
           within it. *)
        fun log2Long(i, byte) =
        let
            val b = loadByte(i, byte)
        in
            if b = 0w0 then log2Long(i, byte-1)
            else log2Word(b, 0w2, 1) + byte*8
        end
            
    in
        fun log2 i =
            if i <= 0 then raise Domain
            else if isShort i
            then log2Word(Word.fromInt i, 0w2, 1)
            else (* i is actually a pointer to a byte segment. *)
            let
                val bytes =
                    segLength i * RunCall.unsafeCast LibrarySupport.wordSize
            in
               log2Long(i, bytes-1)
            end
    end

    (* These are implemented in the RTS. *)
    val orb : int * int -> int = RunCall.run_call2 POLY_SYS_ora
    val xorb : int * int -> int = RunCall.run_call2 POLY_SYS_xora
    val andb : int * int -> int = RunCall.run_call2 POLY_SYS_anda

    (* notb is defined as ~ (i+1) and there doesn't seem to be much advantage
       in implementing it any other way. *)
    fun notb i = ~(i + 1)

    fun pow(i, j) =
    let
        fun power(acc, _, 0) = acc
         |  power(acc, n, i) =
            power(if andb(i, 1) = 1 then acc*n else acc, n*n, Int.quot(i, 2))
    in
        if j < 0
        then (* Various exceptional cases. *)
            (
            if i = 0 then raise Div
            else if i = 1 then 1
            else if i = ~1
            then if andb(j, 1) = 0 then (*even*) 1 else (*odd*) ~1
            else 0
            )
        else power(1, i, j)
    end

    (* These could be implemented in the RTS although I doubt if it's
       really worth it. *)
    fun << (i: int, j: Word.word) = i * pow(2, Word.toInt j)
    and ~>> (i: int, j: Word.word) = Int.div(i, pow(2, Word.toInt j))
        
end;

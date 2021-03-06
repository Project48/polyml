(*
    Title:      Standard Basis Library: Pack Real structures and signatures
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

(* G&R 2004 status: checked: unchanged. *)

signature PACK_REAL =
sig
    type real
    val bytesPerElem : int
    val isBigEndian : bool
    val toBytes   : real -> Word8Vector.vector
    val fromBytes : Word8Vector.vector -> real
    val subVec : Word8Vector.vector * int -> real
    val subArr : Word8Array.array * int -> real
    val update : Word8Array.array * int * real -> unit
end;

local
    open LibrarySupport
    open RuntimeCalls
    open LibrarySupport.Word8Array

    val realSize: word = RunCall.run_call2 POLY_SYS_Real_Dispatch (28, ())
    val System_lock: address -> unit   = RunCall.run_call1 POLY_SYS_lockseg;
    val System_locks: string -> unit   = RunCall.run_call1 POLY_SYS_lockseg;

    local
        val System_loadb: address * word -> Word8.word =
                RunCall.run_call2 POLY_SYS_load_byte;
        val System_setb: address * word * Word8.word -> unit =
                RunCall.run_call3 POLY_SYS_assign_byte;
        val System_move_bytes: address*word*address*word*word->unit =
                RunCall.run_call5 POLY_SYS_move_bytes

        (* Move bytes, reversing the order. *)
        fun swapOrder(src: address, srcOff: word,
                      dest: address, destOff: word,
                      length: word) =
            if length = 0w0 then ()
            else
            (
            System_setb(dest, destOff+length-0w1, System_loadb(src, srcOff));
            swapOrder(src, srcOff+0w1, dest, destOff, length-0w1)
            )
    in
        fun doMove(src: address, srcOff: word,
                   dest: address, destOff: word, wantBigEndian: bool) =
            if wantBigEndian = bigEndian (* Host byte order = required byte order *)
            then System_move_bytes(src, srcOff, dest, destOff, realSize)
            else (* Host byte order is reverse of required byte order. *)
                swapOrder(src, srcOff, dest, destOff, realSize)
    end
in

    structure PackRealBig: PACK_REAL =
    struct
        type real = real

        val bytesPerElem: int = Word.toInt realSize
        val isBigEndian = true (* Note: this seems unnecessary. *)
    
        fun toBytes r =
        let
            val v = allocString realSize
            (* r is actually represented by a pointer to a vector. *)
            val addr: address = RunCall.unsafeCast r
        in
            doMove(addr, 0w0, stringAsAddress v, wordSize, isBigEndian);
            System_locks v;
            Vector v
        end

        fun fromBytes (v as Vector vec) =
        (* Raises an exception if the vector is too small and takes the first
           few elements if it's larger. *)
            if Word8Vector.length v < bytesPerElem
            then raise Subscript
            else
            let
                val r = allocBytes realSize
            in
                doMove(stringAsAddress vec, wordSize, r, 0w0, isBigEndian);
                System_lock r;
                (RunCall.unsafeCast r): real
            end

        fun subVec(v as Vector vec, i) =
        let
            val iW = unsignedShortOrRaiseSubscript i * realSize
        in
            if iW >= Word.fromInt(Word8Vector.length v)
            then raise Subscript (* This IS defined. *)
            else
            let
                val r = allocBytes realSize
            in
                doMove(stringAsAddress vec, wordSize + iW, r, 0w0, isBigEndian);
                System_lock r;
                (RunCall.unsafeCast r): real
            end
        end

        fun subArr(Array(l, v), i) =
        let
            val iW = unsignedShortOrRaiseSubscript i * realSize
        in
            if iW >= l
            then raise Subscript (* This IS defined. *)
            else
            let
                val r = allocBytes realSize
            in
                doMove(v, iW, r, 0w0, isBigEndian);
                System_lock r;
                (RunCall.unsafeCast r): real
            end
        end

        fun update(Array(l, v), i, r) =
        let
            val iW = unsignedShortOrRaiseSubscript i * realSize
        in
            if iW >= l
            then raise Subscript (* This IS defined. *)
            else
            let
                (* r is actually represented by a pointer to a vector. *)
                val addr: address = RunCall.unsafeCast r
            in
                doMove(addr, 0w0, v, iW, isBigEndian)
            end
        end
    end;
    
    structure PackRealLittle: PACK_REAL =
    struct
        type real = real
        val bytesPerElem: int = Word.toInt realSize
        val isBigEndian = false
        fun toBytes r =
        let
            val v = allocString realSize
            (* r is actually represented by a pointer to a vector. *)
            val addr: address = RunCall.unsafeCast r
        in
            doMove(addr, 0w0, stringAsAddress v, wordSize, isBigEndian);
            System_locks v;
            Vector v
        end

        fun fromBytes(v as Vector vec) =
        (* Raises an exception if the vector is too small and takes the first
           few elements if it's larger. *)
            if Word8Vector.length v < bytesPerElem
            then raise Subscript
            else
            let
                val r = allocBytes realSize
            in
                doMove(stringAsAddress vec, wordSize, r, 0w0, isBigEndian);
                System_lock r;
                (RunCall.unsafeCast r): real
            end

        fun subVec(v as Vector vec, i) =
        let
            val iW = unsignedShortOrRaiseSubscript i * realSize
        in
            if iW >= Word.fromInt(Word8Vector.length v)
            then raise Subscript (* This IS defined. *)
            else
            let
                val r = allocBytes realSize
            in
                doMove(stringAsAddress vec, wordSize+iW, r, 0w0, isBigEndian);
                System_lock r;
                (RunCall.unsafeCast r): real
            end
        end

        fun subArr(Array(l, v), i) =
        let
            val iW = unsignedShortOrRaiseSubscript i * realSize
        in
            if iW >= l
            then raise Subscript (* This IS defined. *)
            else
            let
                val r = allocBytes realSize
            in
                doMove(v, iW, r, 0w0, isBigEndian);
                System_lock r;
                (RunCall.unsafeCast r): real
            end
        end

        fun update(Array(l, v), i, r) =
        let
            val iW = unsignedShortOrRaiseSubscript i * realSize
        in
            if iW >= l
            then raise Subscript (* This IS defined. *)
            else
            let
                (* r is actually represented by a pointer to a vector. *)
                val addr: address = RunCall.unsafeCast r
            in
                doMove(addr, 0w0, v, iW, isBigEndian)
            end
        end
    end;
end;

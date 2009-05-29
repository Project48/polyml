(*
    Title:      Standard Basis Library: IO Support functions
    Copyright   David C.J. Matthews 2000

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
(* This function provides wrappers for the RTS file descriptors to construct
   TextPrimIO and BinPrimIO readers and writers.  It is used both from the
   TextIO and BinIO structures and also from the Windows and Unix structures
   to wrap up pipes. *)
structure LibraryIOSupport:
sig
val wrapInFileDescr :
	{ fd : OS.IO.iodesc, name : string, initBlkMode : bool } -> TextPrimIO.reader
val wrapOutFileDescr :
	{ fd : OS.IO.iodesc, name : string, appendMode : bool,
	  initBlkMode : bool, chunkSize : int } -> TextPrimIO.writer
val wrapBinInFileDescr :
	{ fd : OS.IO.iodesc, name : string, initBlkMode : bool } -> BinPrimIO.reader
val wrapBinOutFileDescr :
	{ fd : OS.IO.iodesc, name : string, appendMode : bool,
	  initBlkMode : bool, chunkSize : int } -> BinPrimIO.writer

val readBinVector: OS.IO.iodesc * int -> Word8Vector.vector
val readBinArray: OS.IO.iodesc * Word8ArraySlice.slice -> int
val writeBinVec: OS.IO.iodesc * Word8VectorSlice.slice -> int
val writeBinArray: OS.IO.iodesc * Word8ArraySlice.slice -> int
val nonBlocking : ('a->'b) -> 'a ->'b option
val protect: Thread.Mutex.mutex -> ('a -> 'b) -> 'a -> 'b
end
=
struct
	(* open IO *)
	open RuntimeCalls
	type address = LibrarySupport.address
	type fileDescr = OS.IO.iodesc
	(* Called after any exception in the lower level reader or
	   writer to map any exception other than Io into Io. *)

	(* We have to declare doIo separately depending on the
	   types of the arguments. It's possible to get round this
	   but that would result in an extra call to run_call3 for
	   each io call. *)
	local
		val doIo: int*int*string -> fileDescr
			 = RunCall.run_call3 POLY_SYS_io_dispatch
	in
		val stdInDesc: fileDescr = doIo(0, 0, "")
		and stdOutDesc: fileDescr = doIo(1, 0, "")
		and stdErrDesc: fileDescr = doIo(2, 0, "")

		fun sys_open_in_text name = doIo(3, 0, name)
		and sys_open_in_bin name = doIo(4, 0, name)
		and sys_open_out_text name = doIo(5, 0, name)
		and sys_open_out_bin name = doIo(6, 0, name)
		and sys_open_append_text name = doIo(13, 0, name)
		and sys_open_append_bin name = doIo(14, 0, name)
	end

	local
		val doIo = RunCall.run_call3 POLY_SYS_io_dispatch
	in
		fun sys_close (strm: fileDescr): unit = doIo(7, strm, 0)
		and sys_block_in(strm: fileDescr): unit = doIo(27, strm, 0)
		and sys_block_out(strm: fileDescr): unit = doIo(29, strm, 0)
	end

	local
		val doIo = RunCall.run_call3 POLY_SYS_io_dispatch
	in
		fun sys_read_text (strm: fileDescr, vil: address*word*word): int =
			doIo(8, strm, vil)

		fun sys_write_text (strm: fileDescr, vil: address*word*word): int =
			doIo(11, strm, vil)

		fun sys_read_bin (strm: fileDescr, vil: address*word*word): int =
			doIo(9, strm, vil)

		fun sys_write_bin (strm: fileDescr, vil: address*word*word): int =
			doIo(12, strm, vil)
	end

	local
		val doIo = RunCall.run_call3 POLY_SYS_io_dispatch
	in
		fun sys_read_string (strm: fileDescr, len: int): string =
			doIo(10, strm, len)
	end

	local
		val doIo = RunCall.run_call3 POLY_SYS_io_dispatch
	in
		fun readBinVector (strm: fileDescr, len: int): Word8Vector.vector =
			doIo(26, strm, len)
	end


	local
		val doIo = RunCall.run_call3 POLY_SYS_io_dispatch
	in
		fun sys_get_buffsize (strm: fileDescr): int = doIo(15, strm, 0)
		and sys_can_input(strm: fileDescr): int = doIo(16, strm, 0)
		and sys_can_output(strm: fileDescr): int = doIo(28, strm, 0)
		and sys_avail(strm: fileDescr): int = doIo(17, strm, 0)
		and sys_get_pos(strm: fileDescr): int = doIo(18, strm, 0)
		and sys_set_pos(strm: fileDescr, p: int): unit =
			(doIo(19, strm, p); ())
		and sys_end_pos(strm: fileDescr): int = doIo(20, strm, 0)
		and sys_get_iodesc(strm: fileDescr): int = doIo(30, strm, 0)
	end

	local
		(* Find out the error which will be generated if a stream in
		   non-blocking mode would block. *)
		val eAgain = OS.syserror "EAGAIN" and eWouldBlock = OS.syserror "EWOULDBLOCK"
		and eInProgress = OS.syserror "EINPROGRESS"
	in
		(* If evaluating the function raises EAGAIN or EWOULDBLOCK we return NONE
		   otherwise if it succeeds return SOME result.  Pass other exceptions back
		   to the caller. *)
		fun nonBlocking f arg =
			SOME(f arg) handle exn as OS.SysErr(_, SOME e) =>
				if (case eAgain of SOME again => e = again | NONE => false) then NONE
				else if (case eWouldBlock of SOME wouldBlock => e = wouldBlock | NONE => false) then NONE
				else if (case eInProgress of SOME inProgress => e = inProgress | NONE => false) then NONE
				else raise exn
	end

	val wordSize : word = LibrarySupport.wordSize;
	
	(* Find out if random access is permitted and return the
	   appropriate values. *)
	fun getRandAccessFns n =
	let
		val isRandomAccess =
			((sys_get_pos n; true) handle _ => false)
		val getPos =
			if isRandomAccess
			then SOME(fn () => sys_get_pos n)
			else NONE
		val setPos =
			if isRandomAccess
			then SOME(fn p => sys_set_pos(n, p))
			else NONE
		val endPos =
			if isRandomAccess
			then SOME(fn () => sys_end_pos n)
			else NONE
	in
		(getPos, setPos, endPos)
	end

	fun writeBinArray (n: fileDescr, slice: Word8ArraySlice.slice): int =
	let
		val (buf, i, len) = Word8ArraySlice.base slice
		val LibrarySupport.Word8Array.Array(_, v) = buf
		val iW = LibrarySupport.unsignedShortOrRaiseSubscript i
		val lenW = LibrarySupport.unsignedShortOrRaiseSubscript len
	in
		sys_write_bin(n, (v, iW, lenW))
	end

	fun readBinArray (n: fileDescr, slice: Word8ArraySlice.slice): int =
	let
		val (buf, i, len) = Word8ArraySlice.base slice
		val LibrarySupport.Word8Array.Array(_, v) = buf
		val lenW = LibrarySupport.unsignedShortOrRaiseSubscript len
		val iW = LibrarySupport.unsignedShortOrRaiseSubscript i
	in
		sys_read_bin(n, (v, iW, lenW))
	end

	(* Write out a string using the underlying call.  Note
	   that we have to add the size of a word to the offsets
	   to skip the length word.  The underlying call deals
	   with the special case of a single character string
	   where the "string" is actually the character itself. *)
	fun writeBinVec (n: fileDescr, slice: Word8VectorSlice.slice): int =
	let
		val (buf, i, len) = Word8VectorSlice.base slice
		val LibrarySupport.Word8Array.Vector v = buf
		val iW = LibrarySupport.unsignedShortOrRaiseSubscript i
		val lenW = LibrarySupport.unsignedShortOrRaiseSubscript len
	in
		sys_write_bin(n, (v, iW+wordSize, lenW))
	end


	(* Create the primitive IO functions and add the higher layers.
	   For all file descriptors other than standard input we look
	   at the stream to see if we can do non-blocking input and/or
	   random access. Standard input, though is persistent and so
	   we have to take a more restrictive view. *)
	fun wrapInFileDescr{ fd, name, initBlkMode } =
	let
		fun readArray (slice: CharArraySlice.slice): int =
		let
			val (buf, i, len) = CharArraySlice.base slice
			val LibrarySupport.CharArray.Array(_, v) = buf
			val iW = LibrarySupport.unsignedShortOrRaiseSubscript i
			val lenW = LibrarySupport.unsignedShortOrRaiseSubscript len
		in
			sys_read_text(fd, (v, iW, lenW))
		end

		fun readVector l = sys_read_string(fd, l)

		(* If we have opened the stream in non-blocking mode readVec
		   and readArray will raise an exception if they would block.
		   We have to handle that.  The blocking functions can be
		   constructed using block_in but that should be done by
		   augmentReader. *)
		val (readVec, readArr, readVecNB, readArrNB) =
			if initBlkMode
			then (SOME readVector, SOME readArray, NONE, NONE)
			else (NONE, NONE, SOME(nonBlocking readVector),
					SOME(nonBlocking readArray))

		(* Don't allow random access on stdIn.  The reason is that we
		   create stdIn when we compile TextIO yet this stream is persistent
		   (unlike every other stream). *)
		val (getPos, setPos, endPos) =
			if sys_get_iodesc fd <= 2 then (NONE, NONE, NONE)
			else getRandAccessFns fd

		(* Unlike the other functions "avail" is a function returning
		   an option, not an optional function. *)
		fun avail () =
		let
			(* If we get an exception or a negative number return NONE. *)
			val v = sys_avail fd handle _ => ~1
		in
			if v >= 0 then SOME v else NONE
		end

		val textPrimRd =
			TextPrimIO.RD {
				name = name,
				chunkSize = sys_get_buffsize fd,
				readVec = readVec,
				readArr = readArr,
				readVecNB = readVecNB,
				readArrNB = readArrNB,
				block = SOME(fn () => sys_block_in fd),
				canInput = SOME (fn () => sys_can_input fd > 0),
				avail = avail,
				getPos = getPos,
				setPos = setPos,
				endPos = endPos,
				verifyPos = getPos,
				close = fn () => sys_close fd,
				ioDesc = (SOME fd) : OS.IO.iodesc option
			}
	in
		TextPrimIO.augmentReader textPrimRd
	end

	fun wrapOutFileDescr {fd, name, appendMode, initBlkMode, chunkSize} =
	let
		fun writeArray (slice: CharArraySlice.slice): int =
		let
			val (buf, i, len) = CharArraySlice.base slice
			val LibrarySupport.CharArray.Array(_, v) = buf
			val iW = LibrarySupport.unsignedShortOrRaiseSubscript i
			val lenW = LibrarySupport.unsignedShortOrRaiseSubscript len
		in
			sys_write_text(fd, (v, iW, lenW))
		end

		(* Write out a string using the underlying call.  Note
		   that we have to add the size of a word to the offsets
		   to skip the length word.  The underlying call deals
		   with the special case of a single character string
		   where the "string" is actually the character itself. *)
		fun writeVector (slice: CharVectorSlice.slice): int =
		let
			val (buf, i, len) = CharVectorSlice.base slice
			val v = RunCall.unsafeCast buf
			val iW = LibrarySupport.unsignedShortOrRaiseSubscript i
			val lenW = LibrarySupport.unsignedShortOrRaiseSubscript len
		in
			sys_write_text(fd, (v, iW+wordSize, lenW))
		end

		(* Set up the writers depending on whether the stream is
		   in non-blocking mode or not. *)
		val (writeVec, writeArr, writeVecNB, writeArrNB) =
			if initBlkMode
			then (SOME writeVector, SOME writeArray, NONE, NONE)
			else (NONE, NONE, SOME(nonBlocking writeVector),
					SOME(nonBlocking writeArray))

		(* Random access is provided if getPos works except that we
		   don't allow it for standard output and standard error at all. *)
		val (getPos, setPos, endPos) =
			if sys_get_iodesc fd <= 2 then (NONE, NONE, NONE)
			else getRandAccessFns fd
		(* If we have opened the stream for append we will always
		   write to the end of the stream so setPos won't work. *)
		val setPos = if appendMode then NONE else setPos

		val textPrimWr =
			TextPrimIO.WR {
				name = name,
				chunkSize = chunkSize,
				writeVec = writeVec,
				writeArr = writeArr,
				writeVecNB = writeVecNB,
				writeArrNB = writeArrNB,
				block = SOME(fn () => sys_block_out fd),
				canOutput = SOME(fn () => sys_can_output fd > 0),
				getPos = getPos,
				setPos = setPos,
				endPos = endPos,
				verifyPos = getPos,
				close = fn () => sys_close fd,
				ioDesc = (SOME fd) : OS.IO.iodesc option
			}
	in
		TextPrimIO.augmentWriter textPrimWr
	end

	fun wrapBinInFileDescr{fd, name, initBlkMode} =
	let
		fun readVector l = readBinVector(fd, l)
		and readArray b = readBinArray(fd, b)
		(* If we have opened the stream in non-blocking mode readVec
		   and readArray will raise an exception if they would block.
		   We have to handle that.  The blocking functions can be
		   constructed using block_in but that should be done by
		   augmentReader. *)
		val (readVec, readArr, readVecNB, readArrNB) =
			if initBlkMode
			then (SOME readVector, SOME readArray, NONE, NONE)
			else (NONE, NONE, SOME(nonBlocking readVector),
					SOME(nonBlocking readArray))

		(* Random access is provided if getPos works. *)
		val (getPos, setPos, endPos) = getRandAccessFns fd

		(* Unlike the other functions "avail" is a function returning
		   an option, not an optional function. *)
		fun avail () =
		let
			(* If we get an exception or a negative number return NONE. *)
			val v = sys_avail fd handle _ => ~1
		in
			if v >= 0 then SOME v else NONE
		end

		val binPrimRd =
			BinPrimIO.RD {
				name = name,
				chunkSize = sys_get_buffsize fd,
				readVec = readVec,
				readArr = readArr,
				readVecNB = readVecNB,
				readArrNB = readArrNB,
				block = SOME(fn () => sys_block_in fd),
				canInput = SOME(fn() =>sys_can_input fd > 0),
				avail = avail,
				getPos = getPos,
				setPos = setPos,
				endPos = endPos,
				verifyPos = getPos,
				close = fn() => sys_close fd,
				ioDesc = SOME fd
			}
	in
		BinPrimIO.augmentReader binPrimRd
	end

	fun wrapBinOutFileDescr{fd, name, appendMode, initBlkMode, chunkSize} =
	let
		fun writeArray b = writeBinArray(fd, b)
		and writeVector b = writeBinVec(fd, b)

		(* Set up the writers depending on whether the stream is
		   in non-blocking mode or not. *)
		val (writeVec, writeArr, writeVecNB, writeArrNB) =
			if initBlkMode
			then (SOME writeVector, SOME writeArray, NONE, NONE)
			else (NONE, NONE, SOME(nonBlocking writeVector),
					SOME(nonBlocking writeArray))

		(* Random access is provided if getPos works. *)
		val (getPos, setPos, endPos) = getRandAccessFns fd
		(* If we have opened the stream for append we will always
		   write to the end of the stream so setPos won't work. *)
		val setPos = if appendMode then NONE else setPos

		val binPrimWr =
			BinPrimIO.WR {
				name = name,
				chunkSize = chunkSize,
				writeVec = writeVec,
				writeArr = writeArr,
				writeVecNB = writeVecNB,
				writeArrNB = writeArrNB,
				block = SOME(fn () => sys_block_out fd),
				canOutput = SOME(fn () => sys_can_output fd > 0),
				getPos = getPos,
				setPos = setPos,
				endPos = endPos,
				verifyPos = getPos,
				close = fn () => sys_close fd,
				ioDesc = SOME fd
			}
	in
		BinPrimIO.augmentWriter binPrimWr
	end

    (* Many of the IO functions need a mutex so we include this here.
       This applies a function while a mutex is being held. *)
    fun protect m f a =
    let
        open Thread.Thread Thread.Mutex
        (* Set this to handle interrupts synchronously except if we are blocking
           them.  We don't want to get an asynchronous interrupt while we are
           actually locking or unlocking the mutex but if we have to block to do
           IO then we should allow an interrupt at that point. *)
        val oldAttrs = getAttributes()
        val () =
           case List.find (fn InterruptState _ => true | _ => false) oldAttrs of
               SOME(InterruptState InterruptDefer) => ()
             | _ => setAttributes[InterruptState InterruptSynch];
        val () = setAttributes[InterruptState InterruptSynch]
        val () = lock m
        val result = f a
            handle exn => (unlock m; setAttributes oldAttrs; raise exn)
    in
        unlock m;
        setAttributes oldAttrs;
        result
    end

end;


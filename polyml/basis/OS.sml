(*
    Title:      Standard Basis Library: OS Structures and Signatures
    Author:     David Matthews
    Copyright   David Matthews 2000, 2005

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

(* G&R 2004 status: in progress. Signatures checked.  Minor change to OS_IO.  Structures unchecked.
   OS.Path in particular may have to be checked. It wasn't clearly specified before and I may
   have made assumptions that conflict with the G&R. *)

signature OS_FILE_SYS =
  sig
    type dirstream
    val openDir : string -> dirstream
    val readDir : dirstream -> string option
    val rewindDir : dirstream -> unit
    val closeDir : dirstream -> unit
    val chDir : string -> unit
    val getDir : unit -> string
    val mkDir : string -> unit
    val rmDir : string -> unit
    val isDir : string -> bool
    val isLink : string -> bool
    val readLink : string -> string
    val fullPath : string -> string
    val realPath : string -> string
    val modTime : string -> Time.time
    val fileSize : string -> Position.int

	val setTime : (string * Time.time Option.option) -> unit
    val remove : string -> unit
    val rename : {old : string, new : string} -> unit

    datatype access_mode
      = A_READ
      | A_WRITE
      | A_EXEC

	val access : (string * access_mode list) -> bool

	val tmpName : unit -> string

	eqtype  file_id
    val fileId : string -> file_id
    val hash : file_id -> word
    val compare : (file_id * file_id) -> General.order
  end (* OS_FILE_SYS *);


signature OS_PATH =
sig
	exception Path
	exception InvalidArc
	val parentArc : string
	val currentArc : string

	val fromString : string -> { isAbs : bool, vol : string, arcs : string list }
	val toString : { isAbs : bool, vol : string, arcs : string list } -> string

	val validVolume : {isAbs : bool, vol : string} -> bool
	val getVolume : string -> string
	val getParent : string -> string
	val splitDirFile : string -> {dir : string, file : string}
	val joinDirFile : {dir : string, file : string} -> string
	val dir  : string -> string
	val file : string -> string
	val splitBaseExt : string -> {base : string, ext : string option }
	val joinBaseExt : {base : string, ext : string option} -> string
	val base : string -> string
	val ext  : string -> string option
	val mkCanonical : string -> string
	val isCanonical : string -> bool
	val mkAbsolute : {path : string, relativeTo : string} -> string
	val mkRelative : {path : string, relativeTo : string} -> string
	val isAbsolute : string -> bool
	val isRelative : string -> bool
	val isRoot : string -> bool
	val concat : string * string -> string
    val toUnixPath : string -> string
    val fromUnixPath : string -> string
end (* OS_PATH *);


signature OS_PROCESS =
  sig
	 type status
     val success : status
     val failure : status
     val isSuccess : status -> bool
     val system : string -> status
     val atExit : (unit -> unit) -> unit
     val exit : status -> 'a
     val terminate : status -> 'a
     val getEnv : string -> string Option.option
	 val sleep: Time.time -> unit
  end (* OS_PROCESS *);


signature OS_IO =
  sig
    eqtype  iodesc
    val hash : iodesc -> word
    val compare : (iodesc * iodesc) -> General.order
	
    eqtype  iodesc_kind
    val kind : iodesc -> iodesc_kind
	
    structure Kind:
	  sig
        val file : iodesc_kind
        val dir : iodesc_kind
        val symlink : iodesc_kind
        val tty : iodesc_kind
        val pipe : iodesc_kind
        val socket : iodesc_kind
        val device : iodesc_kind
      end

    eqtype poll_desc
    type  poll_info
    val pollDesc : iodesc -> poll_desc Option.option
    val pollToIODesc : poll_desc -> iodesc

    exception Poll
    val pollIn : poll_desc -> poll_desc
    val pollOut : poll_desc -> poll_desc
    val pollPri : poll_desc -> poll_desc

    val poll : (poll_desc list * Time.time Option.option) -> poll_info list

    val isIn : poll_info -> bool
    val isOut : poll_info -> bool
    val isPri : poll_info -> bool

    val infoToPollDesc : poll_info -> poll_desc

  end (* OS_IO *);


signature OS =
  sig
    eqtype  syserror
    exception SysErr of (string * syserror Option.option)
    val errorMsg : syserror -> string
    val errorName : syserror -> string
    val syserror : string -> syserror Option.option

    structure FileSys : OS_FILE_SYS
    structure Path : OS_PATH
    structure Process : OS_PROCESS
    structure IO : OS_IO
  end (* OS *);


structure OS:> OS =
struct
    type syserror = int (* Abstract. *)

	(* The calls themselves raise the SysCall exception.
	   That has to be turned into a SysError exception. *)
    structure SysErr =
      RunCall.Run_exception1
        (
          type ex_type = string * syserror Option.option
          val ex_iden  = RuntimeCalls.EXC_syserr
        );
	exception SysErr = SysErr.ex

	local
		val doCall: int*syserror -> string
			 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
	in
		(* Convert a numeric system error to a string. *)
		fun errorName (s: syserror) : string = doCall(2, s)
		fun errorMsg (s: syserror) : string = doCall(3, s)
	end

	local
		val doCall: int*string -> syserror
			 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
	in
		(* Convert a string to an error message if possible. *)
		fun syserror (s: string) : syserror option =
		let
			val n = doCall(4, s)
		in
			if n = 0 then NONE else SOME n
		end
	end



	structure Path:> OS_PATH =
	struct
		(* Note: The definition of relative and absolute paths are
		   somewhat unclear and some of the examples seem contradictory.
		   The definition I would prefer to use is that an absolute path
		   is one which identifies a given file independent of any setting
		   of the current directory.  Hence the examples of "\" and "\A\B" as
		   being absolute paths in DOS is in my opinion wrong.  These are
		   relative since they depend on the setting of the current volume.
		   However this is a mess when it comes to fromString since if
		   we don't treat "\A" as an absolute path it looks just like an
		   absolute path with an empty arc. *)
		exception Path
	    exception InvalidArc

		local
			val doCall: int*int -> string
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			val parentArc = doCall (6, 0)
			and currentArc = doCall (5, 0)
			and separator = doCall (7, 0)
		end

		local
			val doCall: int*Char.char -> bool
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			(* Assume for the moment that separators are always single chars. *)
			fun isSeparator ch = doCall (8, ch)

			(* Internal - are names case sensitive? Yes in Unix, no in Windows. *)
			val isCaseSensitive = doCall (9, #" ")

			(* Internal - is an empty arc redundant? i.e. is // equivalent to / ?
			   In Windows it isn't at the start of a path but we will already have
			   removed anything that looks like a volume by the time we come to
			   canonicalise it. *)
			val emptyArcIsRedundant = doCall (10, #" ")
		end

		local
			val doCall: int*(string*bool) -> string
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			(* Internal function - construct the prefix for a volume. *)
			(* Same for Unix and Windows. *)
			fun makePrefix(vol: string, isAbs: bool) = doCall(12, (vol, isAbs))
		end

		local
			val doCall: int*string -> bool
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			(* Internal function - returns false if the string contains
			   an invalid character.  Arc separators are allowed. *)
			fun isValidArc s = doCall(13, s)
		end

		local
			val doCall: int*string -> int*string*bool =
				RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			fun matchVolumePrefix s = doCall(11, s)
		end

		(* (* Windows *)
		fun matchVolumePrefix (s: string): int*string*bool =
		(* Given a string it examines the prefix and extracts the volume
		   name if there is one.  It returns the volume and also whether
		   the name is absolute.  It also returns the number of characters
		   which matched so that this can be removed before treating
		   the rest as a relative path. *)
		let
			val slen = String.size s
		in
			if slen = 0 then (0, "", false)
			else if slen >= 2 andalso String.sub(s, 1) = #":" andalso
					Char.isAlpha(String.sub(s, 0))
			then
				if slen > 2 andalso isSeparator(String.sub(s, 2))
				then (3, String.substring(s, 0, 2), true) (* e.g. C:\ or C:\fred *)
				else (2, String.substring(s, 0, 2), false) (* e.g. C: or C:fred *)

			else if slen > 2 andalso isSeparator(String.sub(s, 0))
					andalso isSeparator(String.sub(s, 1))
			then (* Looks like a UNC server name. See how big it is. *)
			let
				val (server, rest) =
					Substring.splitl(fn c => not (isSeparator c))
						(Substring.extract(s, 2, NONE))
				(* TODO: Is the server name actually valid?  Assume yes. *)
			in
				if Substring.size rest = 0
				then (0, "", false)
				else (* Must be room for a share name as well. *)
				let
					val shareName =
						Substring.takel(fn c => not (isSeparator c))
								(Substring.triml 1 rest)
				in
					(Substring.size server + Substring.size shareName + 4,
						separator ^ separator ^ 
							Substring.string server ^ separator ^
							Substring.string shareName, true)
				end
			end
			else if isSeparator(String.sub(s, 0))
			then (* Treat it as absolute even though it really isn't *)
				(1, "", true)

			else (0, "", false)
		end
		*)
		(* (* Unix: *)
		fun matchVolumePrefix (s: string): int*string*bool =
			if String.size s > 0 andalso String.sub(s, 0) = "/"
			then (1, "", true)
			else (0, "", false) 
		*)

		(* Internal - map the strings to the canonical case if they
		   are not case sensitive. *)
		val toCanonicalCase =
			if isCaseSensitive then fn s => s
			else String.map Char.toLower

		(* Internal - are the arcs equivalent? *)
		fun equivalent (s, t) = toCanonicalCase s = toCanonicalCase t

		(* See if the volume name is valid. *)
		fun validVolume {isAbs, vol} =
		let
			(* Convert it to full volume prefix form then try to
			   match that. *)
			val fullVol = makePrefix(vol, isAbs)
			val (nLen, _, abs) = matchVolumePrefix fullVol
		in
			(* If we matched the whole name and it had the same
			   absolute/relative status then we can assume that it
			   was a valid volume name. *)
			nLen = String.size fullVol andalso abs = isAbs
		end

		(* Note: The examples for Unix paths are a mess.  There is considerable
		   confusion in the examples between "/" meaning the root directory
		   and its use as a path separator.  *)
		fun fromString (s: string) =
		let
			(* Do we have a volume name? *)
			val (volLen, vol, abs) = matchVolumePrefix  s
			(* The remainder forms a set of arcs. *)
			val rest = String.extract(s, volLen, NONE)
			(* String.fields returns a single empty string when given
			   the empty string.  I'm not sure whether that's right or not
			   but it's not what we want. *)
			val arcs =
				if rest = "" then []
				else String.fields isSeparator rest
		in
			{isAbs = abs, vol = vol, arcs=arcs}
		end

		(* Note: This is a mess as well.  For example it says that it should
		   raise Path if there is a relative path which begins with an
		   empty arc.  That's only true in Unix.  What it should say is
		   that it if isAbs is false then it should raise Path if the
		   resulting path has the form of an absolute path. In Windows
		   we should raise path if given (e.g.)
		   {isAbs=false, vol="", arcs=["", "", "a", "b"]} because that
		   looks like a UNC name. *)
		fun toString {isAbs : bool, vol : string, arcs : string list} =
			(* Check we have a valid volume. *)
			if not (validVolume{isAbs=isAbs, vol=vol})
			then raise Path
			(* Check that each arc is valid. *)
			else if List.find (fn s => not (isValidArc s)) arcs <> NONE
			then raise InvalidArc
			else
			let
				(* Place separators between each arc. *)
				fun arcsToLinks [] = []
				  | arcsToLinks [a] = [a]
				  | arcsToLinks (a::b) =
			  		a :: separator :: arcsToLinks b
				val r = String.concat(makePrefix(vol, isAbs) :: arcsToLinks arcs)
				(* Check to see whether we have turned a relative path into
				   an absolute one by including empty arcs in the wrong places. *)
				val (_, _, nowAbs) = matchVolumePrefix r
			in
				if nowAbs <> isAbs
				then raise Path
				else r
			end

		(* Note: this is just defined to "return the volume portion" but
		   doesn't say what to do if there isn't a volume.  Seems simplest
		   to define it as below. *)
		fun getVolume s = #vol(fromString s)

		(* Note: Once again this has very much a Unix view of the world,
		   most of which almost works in Windows.  I don't think MacOS actually
		   has the concept of a parent directory. *)
		fun getParent "" = parentArc
		 |  getParent s =
		 	let
				val len = String.size s
				val (volLen, _, _) = matchVolumePrefix s
				(* Split it at the last separator. *)
				val (prefix, suffix) =
					Substring.splitr (fn c => not (isSeparator c))
						(Substring.full s) 
			in
				if volLen = len
				then s (* We have a root. *)
			 	else if Substring.size suffix = 0
				then
			 		(* If the last character is a separator just add on
					   the parent arc (..) to refer to the parent directory.
					   I don't know why we can't just remove the last component
					   in this case but the examples don't do that.  The only
					   special case is where we have reached the root when
					   we just return the root. *)
					s ^ parentArc
				else if Substring.size prefix = 0
				then (* No separator at all *)
					(
					if s = parentArc (* .. => ../.. *)
					then parentArc ^ (separator) ^ parentArc
					else if s = currentArc
					then parentArc (* . => .. *)
					else currentArc (* abc => . *)
					)
				else if Substring.size prefix = volLen
				(* ??? If the prefix matches the volume then return
				   the whole of prefix including the separator. *)
				then Substring.string prefix
				else (* Return the prefix with the separator removed. *)
					Substring.string(Substring.trimr 1 prefix)
			end

		(* Another mess defined in terms of examples for Unix from which
		   one is expected to infer a general rule.
		   It seems to split the string at the last separator and
		   return the two halves without the separator except in the
		   case where the directory is a root directory when a full
		   volume name and separator are given. *)
		fun splitDirFile s =
		let
			(* Split it at the last separator. *)
			val (prefix, suffix) =
				Substring.splitr (fn c => not (isSeparator c))
					(Substring.full s) 
			val (volLen, vol, _) = matchVolumePrefix s
			val dirName =
				if Substring.size prefix = 0
				then ""
				else Substring.string(Substring.trimr 1 prefix)
			and fileName = Substring.string suffix
		in
			if volLen <> 0 andalso vol = dirName
			then {dir = vol ^ separator, file = fileName}
			else {dir = dirName, file = fileName}
		end

		fun dir s = #dir(splitDirFile s)
		and file s = #file(splitDirFile s)

		(* Question: It seems from the definition of toString that the
		   arcs list can include separators.  Is that true here?
		   Assume yes. *)
		(* If the last character is already a separator we don't add one,
		   e.g. if the directory is "/". *)
		fun joinDirFile{dir, file} =
			if not (isValidArc file) then raise InvalidArc
			else if dir = "" then file (* Return the file name unchanged *)
			else if isSeparator(String.sub(dir, size dir - 1))
			then dir ^ file
			else dir ^ separator ^ file

		fun splitBaseExt s =
		let
			val slen = String.size s
			fun getExt n =
				if n <= 0 then NONE (* If it's at the start ignore it. *)
				else if isSeparator(String.sub(s, n))
				then NONE
				else if String.sub(s, n) = #"."
				then (* Found a dot. *)
					(
					if n = slen-1 then NONE (* Dot in last position. *)
					else if isSeparator(String.sub(s, n-1))
					then NONE (* Dot immediately after separator. *)
					else SOME n
					)
				else getExt (n-1)
			val extPos = getExt(slen - 1)
		in
			case extPos of
				NONE => {base=s, ext=NONE}
			|	SOME n => {base=String.substring(s, 0, n),
						   ext=SOME(String.substring(s, n+1, slen-n-1))}
		end

		fun joinBaseExt {base : string, ext = NONE} = base
		 |  joinBaseExt {base : string, ext = SOME ""} = base
		 |  joinBaseExt {base : string, ext = SOME ext} = base ^ "." ^ ext
	
	    fun base s = #base(splitBaseExt s)
		and ext s = #ext(splitBaseExt s)

		fun mkCanonical s =
		let
			val {isAbs, vol, arcs} = fromString s
			fun collapse [] = []
			  | collapse (a :: b) =
			  		(* Work down the list removing currentArc entries and
					   null entries (if the OS treats them as redundant).. *)
			  		if a = currentArc orelse (emptyArcIsRedundant andalso a = "")
					then collapse b
					(* Then work back up it removing parentArc entries. *)
					else
						case collapse b of
						[] => [a]
					  | (x :: y) =>
					  		if x = parentArc
							then (* Remove "a". *) y
					  		else a :: x :: y

			val collapsed = collapse arcs

			(* If this is the root we can remove leading occurrences of
			   the parent arc since the parent of the root is the root. *)
			fun removeLeadingParent [] = []
			  | removeLeadingParent (a::b) =
			  		if a = parentArc then removeLeadingParent b else a::b
			val newArcs =
				if isAbs then removeLeadingParent collapsed else collapsed
			val res = toString{isAbs=isAbs, vol=vol, arcs=newArcs}
		in
			(* Finally replace the empty string with "." and map to lower case
			   if it's not case sensitive. *)
			if res = "" then currentArc
			else toCanonicalCase res
		end

		fun isCanonical s = mkCanonical s = s handle Path => false

		fun isAbsolute s = #isAbs(fromString s)
		and isRelative s = not(#isAbs(fromString s))

		(* Concatenate two paths.  The second must be relative and, if it
		   contains a volume name, refer to the same volume as the first. *)
		fun concat(s, t) =
		let
			val {isAbs=absS, vol=volS, arcs=ArcsS} = fromString s
			val {isAbs=absT, vol=volT, arcs=ArcsT} = fromString t

			(* Concatenate the two lists of arcs except that a trailing
			   empty arc on the first path is removed
			   (i.e. concat("a/", "b") is the same as concat("a", "b") *)
			fun concatArcs [] p = p
			 |  concatArcs [a] p = if a = "" then p else a :: p 
			 |  concatArcs (a::b) p = a :: concatArcs b p 
		in
			if absT then raise Path
			else if volT <> "" andalso not(equivalent(volS, volT))
			then raise Path
			else toString{isAbs=absS, vol=volS, arcs=concatArcs ArcsS ArcsT}
		end

		(* Make an absolute path by treating a relative path as relative to
		   a given path. *)
		fun mkAbsolute {path, relativeTo} =
		let
			val {isAbs=absP, vol=volP, ...} = fromString path
			val {isAbs=absRT, vol=volRT, ...} = fromString relativeTo
		in
			if absP then path
			else if not absRT then raise Path
			(* If the path contained a volume it must be the
			   same as the absolute path. *)
			else if volP <> "" andalso not(equivalent(volP, volRT))
			then raise Path
			else mkCanonical(concat(relativeTo, path))
		end

		(* Make a relative path by treating an absolute path as derived
		   from a given other absolute path. *)
		fun mkRelative {path, relativeTo} =
			case fromString path of
				{isAbs=false, ...} => path (* Already relative *)
			 |	{vol=volP, arcs=arcsP, ...} =>
				let
					val {isAbs=absRT, vol=volRT, arcs=arcsRT} =
						fromString (mkCanonical relativeTo)
		
					(* Add as many parent arcs as there are arcs in the path. *)
					fun addParents [] p = p
					 |  addParents (a::b) p = parentArc :: addParents b p

					fun matchPaths [] [] = [currentArc] (* Both equal *)
					 |  matchPaths p  [] = (* Absolute path is finished - return p *) p
					 |  matchPaths [] r = (* Relative paths finished - add parent arcs *)
					 						addParents r []
					 |  matchPaths (p :: p') (r :: r') =
					 		(* Are they the same arc?  Note: When arcs are
							   case insensitive I'm doing a case insensitive match
							   here.  *)
					 		if equivalent(p, r)
							then matchPaths p' r'
							else addParents (r :: r') (p :: p')
				in
					if not absRT then raise Path
					(* If the path contained a volume it must be the
					   same as the absolute path. *)
					else if volP <> "" andalso not(equivalent(volP, volRT))
					then raise Path
					else toString{isAbs=false, vol="", arcs=matchPaths arcsP arcsRT}
				end

		(* Note: assume that "a root directory" is one which satisfies
		   the property that it has no parent. Hence the volume name
		   must match the whole string. *)
	    fun isRoot s =
		let
			val (volLen, vol, isAbs) = matchVolumePrefix  s
		in
			isAbs andalso volLen = String.size s andalso isCanonical s
		end

		(* Question: there's no definition of what these functions mean.  The crucial
		   questions are how to deal with volume names and also how to deal
		   with symbols in the paths which may be invalid (e.g. path separators) in
		   one or other system.  For instance "a\b" is a valid file name in Unix
		   and 31/3/2000 is valid in MacOS.
		   Are they supposed to represent the original file system in some way? *)
		fun toUnixPath s =
		let
			(* We may have occurrences of "/" in the arcs if that is not
			   a separator on this OS.  Replace them by this machine's separator. *)
			val convArc =
				String.translate (
					fn ch => if ch = #"/" then separator else String.str ch)
			fun mapArc a =
				if a = currentArc then "."
				else if a = parentArc then ".."
				else a

			fun mapArcs [] = []
			 |  mapArcs [a] = [mapArc a]
			 |  mapArcs (a::b) = mapArc a :: "/" :: mapArcs b

			val {isAbs, vol, arcs} = fromString s
			val volArc = if vol <> "" then vol :: arcs else arcs
			val sl = String.concat(mapArcs volArc)
		in
			if String.size sl = 0 then ""
			else if isAbs then if String.sub(sl, 0) <> #"/" then "/" ^ sl else sl
			else (* not abs *) if String.sub(sl, 0) = #"/" then "." ^ sl else sl
		end

		fun fromUnixPath s =
		let
			val arcs = String.fields (fn ch => ch = #"/") s
			(* Turn any occurrences of this OS's separator into / since
			   that can't occur within an arc. *)
			val convArc =
				String.translate (
					fn ch => if isSeparator ch then "/" else String.str ch)
			val convArcs = List.map convArc arcs
		in
			case convArcs of
				[] => ""
			|   ("" :: a :: rest) =>
				let (* We had a leading / : is the first arc a volume name? *)
					val (n, vol, _) = matchVolumePrefix a
				in
					if n = String.size a
					then (* We have a volume name. *)
						toString{isAbs=true, vol=vol, arcs=rest}
					else toString{isAbs=true, vol="", arcs=convArcs}
				end
			|	(a :: rest) =>
				let (* May be a relative volume name. *)
					val (n, vol, _) = matchVolumePrefix a
				in
					if n = String.size a
					then toString{isAbs=false, vol=vol, arcs=rest}
					else toString{isAbs=false, vol="", arcs=convArcs}
				end
		end

	end (* Path *)

	structure FileSys:> OS_FILE_SYS =
	struct
		type dirFd = int
		(* The directory stream consists of the stream identifier
		   returned by openDir together with the original directory
		   name.  We need that for rewind in Windows. *)
		datatype dirstream = DIR of dirFd * string

		local
			val doIo: int*unit*string -> dirFd
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
	    	fun openDir (s : string):  dirstream = 
				DIR(doIo(50, (), s), s)
		end

		local
			val doIo: int*dirFd*unit -> string
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
	    	fun readDir (DIR(d, _)):  string option =
			let
				(* This returns the empty string at end-of-stream. *)
				val s = doIo(51, d, ())
			in
				if s = "" then NONE else SOME s
			end
		end

		local
			val doIo: int*dirFd*unit -> unit
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			fun closeDir(DIR(d, _)) =
				doIo(52, d, ())
		end

		local
			val doIo: int*dirFd*string -> unit
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* We need to pass in the string because Windows
			   has to reopen the stream. *)
			fun rewindDir(DIR(d, s)) =
				doIo(53, d, s)
		end

		local
	    	val cd: string -> unit =
				RunCall.run_call1 RuntimeCalls.POLY_SYS_chdir;
		in
			fun chDir s = cd s
		end

		local
			val doIo: int*unit*unit -> string
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Return current directory. *)
			fun getDir() = doIo(54, (), ())
			(* Get a temporary file name. *)
			fun tmpName() = doIo(67, (), ())
		end

		local
			val doIo: int*unit*string -> unit
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Create and delete directories and remove a file. *)
			fun mkDir s = doIo(55, (), s)
			and rmDir s = doIo(56, (), s)
			and remove s = doIo(64, (), s)
		end

		local
			val doIo: int*unit*string -> bool
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Test for directory and symbolic link. *)
			fun isDir s = doIo(57, (), s)
			and isLink s = doIo(58, (), s)
		end

		local
			val doIo: int*unit*string -> string
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Read a symbolic link. *)
			fun readLink s = doIo(59, (), s)
			(* Get a full canonical path name. *)
			and fullPath s = doIo(60, (), s)
		end

		local
			val doIo: int*unit*string -> Time.time
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Get file modification time. *)
			fun modTime s = doIo(61, (), s)
		end

		local
			val doIo: int*unit*string -> int
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Get file size. *)
			fun fileSize s = doIo(62, (), s)
		end

		local
			val doIo: int*string*Time.time -> unit
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Get file size. *)
			fun setTime(s, NONE) = doIo(63, s, Time.now())
			 | setTime(s, SOME t) = doIo(63, s, t)
		end

		local
			val doIo: int*string*string -> unit
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Rename a file. *)
			fun rename {old, new} = doIo(65, old, new)
		end

	    datatype access_mode
	      = A_READ
	      | A_WRITE
	      | A_EXEC

		local
			val doIo: int*string*word -> bool
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch

			fun mapAccess (A_READ, m) = Word.orb(m, 0w1)
			 |  mapAccess (A_WRITE, m) = Word.orb(m, 0w2)
			 |  mapAccess (A_EXEC, m) = Word.orb(m, 0w4)
		in
			(* Get access rights. *)
			fun access (s, m) = doIo(66, s, List.foldl mapAccess 0w0 m)
		end

		(* file_id seems to be intended to reflect the semantics of
		   a Unix inode.  That concept doesn't exist in Windows so
		   we use a canonical file name instead. *)
		datatype file_id =
			INODE of int | FILENAME of string

		fun compare(INODE i, INODE j) = Int.compare(i, j)
		  | compare(FILENAME s, FILENAME t) = String.compare(s, t)
		  | (* These cases shouldn't happen but we'll define them
		       anyway. *)
		    compare(INODE _, FILENAME _) = General.GREATER
		  | compare(FILENAME _, INODE _) = General.LESS

		(* TODO: The hash function is supposed to well distribute the
		   the values when taken modulo 2^n for any n.
		   I'm sure we can come up with something better than this. *)
		fun hash(INODE i) =
			let
				open Word
				infix xorb <<
				val w = Word.fromInt i
			in
				w xorb (w << 0w8) xorb (w << 0w16) xorb (w << 0w24)
			end

		 |  hash(FILENAME s) =
		 		(* Simple hash function which multiplies the accumulator
				   by 7 and adds in the next character. *)
		 		CharVector.foldl
					(fn(c, a) => a * 0w7 + Word.fromInt(Char.ord c)) 0w0 s
		local
			val doIo: int*unit*string -> int
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Get file id (inode).  Returns negative value if inodes aren't
			   supported. *)
			fun fileId s =
			let
				val i = doIo(68, (), s)
			in
				if i < 0
				then FILENAME(fullPath s)
				else INODE i
			end
		end

		fun realPath p =
			if Path.isAbsolute p
            then fullPath p
            else Path.mkRelative{path=fullPath p, relativeTo=fullPath(getDir())}
	end (* FileSys *)

	structure IO :> OS_IO =
	struct
		datatype iodesc = 
			IODESC of int (* Actually abstract.  This isn't
							the file descriptor itself, rather
							a pointer into the io table. *)
		local
			val doIo: int*iodesc*unit -> int
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			(* Get underlying index. *)
			fun getIndex f = doIo(69, f, ())
		end

		(* TODO: The hash function is supposed to well distribute the
		   the values when taken modulo 2^n for any n.
		   I'm sure we can come up with something better than this. *)
		fun hash (i: iodesc) : word =
		let
			open Word
			infix xorb <<
			val w = Word.fromInt(getIndex i)
		in
			w xorb (w << 0w8) xorb (w << 0w16) xorb (w << 0w24)
		end

		fun compare(i, j) = Int.compare(getIndex i, getIndex j)

	    (* eq *)type iodesc_kind = int
		
	    structure Kind =
		struct
			val file : iodesc_kind = 0
	        val dir : iodesc_kind = 1
	        val symlink : iodesc_kind = 2
	        val tty : iodesc_kind = 3
	        val pipe : iodesc_kind = 4
	        val socket : iodesc_kind = 5
	        val device : iodesc_kind = 6
		end

		local
			val doIo: int*iodesc*int -> int
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
		    fun kind (i: iodesc): iodesc_kind =
			let
				val k = doIo(21, i, 0)
			in
				(* Returns a negative number if the call fails,
				   otherwise one of the above numbers. *)
				if k < 0 orelse k > 6
				then raise SysErr("Invalid result", NONE)
				else k
			end
		end

		(* The poll descriptor and the result of polling is a
		   bit map together with the io descriptor. *)
		val inBit = 0w1 and outBit = 0w2 and priBit = 0w4

		(* N.B. The implementation of poll_desc is hard-wired into
		   Socket.pollDesc. *)
		type      poll_desc = word*iodesc
		datatype  poll_info = PI of word*poll_desc

		local
			val doIo: int*iodesc*int -> word
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			fun sys_poll_test(i: iodesc) = doIo(22, i, 0)
		end

		local
			val doIo: int*int*
				(iodesc Vector.vector * word Vector.vector * Time.time) ->
						word Vector.vector
				 = RunCall.run_call3 RuntimeCalls.POLY_SYS_io_dispatch
		in
			fun sys_poll_block(iov, wv) = doIo(23, 0, (iov, wv, Time.zeroTime))
			fun sys_poll_poll(iov, wv) = doIo(25, 0, (iov, wv, Time.zeroTime))
			and sys_poll_wait (iov, wv, t) = doIo(24, 0, (iov, wv, t))
		end


		fun pollDesc (i: iodesc): poll_desc option =
		(* If the poll test returns zero then polling is
		   not allowed for any mode. *)
			if sys_poll_test i = 0w0
			then NONE
			else SOME(0w0, i)

		fun pollToIODesc(_, i): iodesc = i

		exception Poll

		(* Add the appropriate bit to the set if it is allowed. *)
		local
			fun addBit b ((bm, i)) =
				if Word.andb(sys_poll_test i, b) = 0w0
				then raise Poll
				else (Word.orb(bm, b), i)
		in
			val pollIn = addBit inBit
			and pollOut = addBit outBit
			and pollPri = addBit priBit
		end

		fun poll (l : poll_desc list, t: Time.time Option.option) :
			poll_info list =
		let
			(* Turn the list into vectors of io descriptors and
			   request bits - easier for the RTS to manage. 
			   N.B.  This assumes that Vector.vector creates a simple memory vector and
			   does not wrap it in any way. *)
			val ioVector: iodesc Vector.vector =
				Vector.fromList(List.map(fn(_, i) => i) l)
			val bitVector: word Vector.vector =
				Vector.fromList(List.map(fn(b, _) => b) l)
			val nDescs = Vector.length ioVector
			(* Do the actual polling.  Returns a vector with bits
			   set for the results. *)
			val resV: word Vector.vector =
				case t of
					NONE => sys_poll_block(ioVector, bitVector)
				|	SOME tt =>
						if tt = Time.zeroTime
						then sys_poll_poll(ioVector, bitVector)
						else if tt < Time.zeroTime
						(* Must check for negative times since these can be
						   interpreted as infinity. *)
						then raise SysErr("Invalid time", NONE)
						(* For non-zero times we convert this to a number of
						   milliseconds since the current time.  We have to
						   pass in an absolute time rather than a relative
						   time because the RTS may retry this call if the
						   polled events haven't happened. *)
						else sys_poll_wait(ioVector, bitVector, tt + Time.now())
			(* Convert the results into a list of poll infos ignoring
			   zero results. *)
			fun resList(_, []) = []
			 |  resList(i, pd :: pdl) =
				let
					val r = Vector.sub(resV, i)
				in
					if r <> 0w0
					then PI(r, pd) :: resList(i+1, pdl)
					else resList(i+1, pdl)
				end
		in
			resList(0, l)
		end

		fun isIn(PI(b, _)) = Word.andb(b, inBit) <> 0w0
		and isOut(PI(b, _)) = Word.andb(b, outBit) <> 0w0
 		and isPri(PI(b, _)) = Word.andb(b, priBit) <> 0w0

		fun infoToPollDesc (PI(_, pd)) = pd

	end (* IO *)

	structure Process:> OS_PROCESS =
	struct

		type status = int

		local
			val doCall: int*unit -> int
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			val success = doCall(15, ())
			and failure = doCall(16, ())
		end

		fun isSuccess i = i = success

		local
			val doCall: int*string -> status
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			(* Run a process and wait for the result. *)
			fun system s = doCall(17, s)
		end
		
		local
			val doCall: int*(unit->unit) -> unit
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			(* Register a function to be run at exit. *)
			fun atExit f = doCall(18, f)
		end

		local
			val doExit =
				RunCall.run_call1 RuntimeCalls.POLY_SYS_exit
			val doCall: int*unit -> (unit->unit) =
				RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			fun exit (n: int) =
			let
				val exitFun =
					(* If we get an empty list here we've finished. *)
					doCall(19, ()) handle _ => doExit n
			in
				(* Run the function and then repeat. *)
				exitFun() handle _ => ();
				exit(n)
			end
		end

		(* Terminate without running the atExit list or flushing the
		   buffers.  The type returns a type variable so we don't
		   use the local...in...end mechanism to reduce extra calls,
		   but it hardly matters since we'll only call this at most
		   once per session! *)
		fun terminate n =
			RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env(20, n)

		local
			val doCall: int*string -> string
				 = RunCall.run_call2 RuntimeCalls.POLY_SYS_process_env
		in
			(* Get an environment string.  The underlying call raises an
			   exception if the string isn't there. *)
			fun getEnv s =
				SOME(doCall(14, s)) handle _ => NONE
		end

		(* poll is implemented so that an empty list simply waits for
		   the time. *)
		fun sleep t = (IO.poll([], SOME t); ())
	end (* Process. *)

end;

local
	(* Install the pretty printer for OS.IO.Kind and OS.syserror.
	   This must be done outside
	   the structure if we use opaque matching. *)
	fun kind_string k =
		if k = OS.IO.Kind.file then "file"
		else if k = OS.IO.Kind.dir then "dir"
		else if k = OS.IO.Kind.symlink then "symlink"
		else if k = OS.IO.Kind.tty then "tty"
		else if k = OS.IO.Kind.pipe then "pipe"
		else if k = OS.IO.Kind.socket then "socket"
		else if k = OS.IO.Kind.device then "device"
		else "unknown"

	fun printKind(p, _, _, _) _ _ x = p(kind_string x)
	fun printSysError(p, _, _, _) _ _ x = p(OS.errorName x)
in
	val it = RunCall.Inner.install_pp printKind
	val it = RunCall.Inner.install_pp printSysError
end

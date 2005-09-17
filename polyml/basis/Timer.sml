(*
    Title:      Standard Basis Library: Timer Signature and structure.
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

(* G&R 2004 status: updated.  Added checkCPUTimes. *)

signature TIMER =
sig
	type cpu_timer
	type real_timer
	val startCPUTimer : unit -> cpu_timer
	val checkCPUTimer : cpu_timer -> {usr : Time.time, sys : Time.time}
	val checkGCTime : cpu_timer -> Time.time
	val totalCPUTimer : unit -> cpu_timer
	val startRealTimer : unit -> real_timer
	val checkRealTimer : real_timer -> Time.time
	val totalRealTimer : unit -> real_timer
	
	val checkCPUTimes : cpu_timer ->
		{
			nongc: { usr : Time.time, sys : Time.time},
			gc: { usr : Time.time, sys : Time.time}
		}
end

structure Timer :> TIMER =
struct
	type cpu_timer = {userTime: Time.time, sysTime: Time.time, gcUTime: Time.time, gcSTime: Time.time }
	type real_timer = Time.time

	local
		val doCall: int*unit -> Time.time
			 = RunCall.run_call2 RuntimeCalls.POLY_SYS_timing_dispatch
		fun getUserTime() = doCall(7, ())
		and getSysTime() = doCall(8, ())
		and getGCUTime() = doCall(9, ())
		and getGCSTime() = doCall(13, ())
	in
		fun startCPUTimer () =
			{userTime=getUserTime(),
			 sysTime=getSysTime(),
			 gcUTime=getGCUTime(),
			 gcSTime=getGCSTime() }
		and checkCPUTimer ({ userTime, sysTime, ...}: cpu_timer) =
			{ usr = getUserTime() - userTime, sys = getSysTime() - sysTime}
		and checkGCTime ({ gcUTime, ...}: cpu_timer) = getGCUTime() - gcUTime
		and totalCPUTimer () =
			{ userTime=Time.zeroTime, sysTime=Time.zeroTime, gcUTime=Time.zeroTime, gcSTime=Time.zeroTime }
		and checkCPUTimes { userTime, sysTime, gcUTime, gcSTime } =
			{ nongc = { usr = getUserTime() - userTime, sys = getSysTime() - sysTime },
			  gc = { usr = getGCUTime() - gcUTime, sys = getGCSTime() - gcSTime } }

		fun totalRealTimer() = Time.zeroTime
		and startRealTimer() = doCall(10, ())
		and checkRealTimer t = startRealTimer() - t
	end

end;


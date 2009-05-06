package pushstream;

import java.util.Date;

/**
 * 
 * a character push stream that sends strings on to a string push stream. 
 * It does this when a time limit is hit, or when no more data is waiting to be pushed. 
 * 
 * @author ldixon
 *
 */
public class TimelyCharToStringStream implements PushStream<Character>{

	int timeLimit;
	Long pushTime;
	String pushString;
	PushStream<String> stream;
	
	public TimelyCharToStringStream(PushStream<String> s, int t) {
		timeLimit = t;
		pushString = new String();
		stream = s;
		pushTime = null;
	}
	
	public void resetPushTime() {
		pushTime = (new Date()).getTime() + timeLimit;
	}
	
	public void add(Character c, boolean isMore) {
		// add character to string
		pushString += c;
		
		// if there was delay set before, make the new push time
		if(pushTime == null) { resetPushTime(); }
		
		if(pushTime < (new Date()).getTime()) { // time limit hit; push string onwards
			stream.add(pushString);
			pushString = new String();
			if(isMore) { resetPushTime(); }
			else { pushTime = null; }
		} else if(! isMore) { // if no more data waiting, push this string on. 
			stream.add(pushString);
			pushString = new String();
			pushTime = null;
		}
	}

	public void add(Character c) { add(c, false); }
	
	public void close() {
		stream.add(pushString);
		stream.close();
		pushString = null;
		pushTime = null;
	}
}
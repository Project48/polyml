package polyml;

import java.io.BufferedReader;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;

import javax.swing.text.BadLocationException;
import javax.swing.text.Element;
import javax.swing.text.html.HTMLDocument;
import javax.swing.text.html.HTMLDocument.BlockElement;

import org.gjt.sp.jedit.jEdit;

public class StateViewDocument {

	/** Internal document */
	private final HTMLDocument doc;
	/** Root to which all text will be appended for now. */
	private Element appendRoot;
	
	/**
	 * Defines how status messages will be displayed.
	 * TODO: allow customisation
	 */
	private static final String StyleSheet = ""+
		"body {\n"+
		"	color: black;\n"+
		"	background-color: #DDD;\n"+
		"}\n"+
		"."+CompileResult.STATUS_SUCCESS+" {\n" +
		"	color: green;\n" +
		"}\n" +
		"."+CompileResult.STATUS_PRELUDE_FAILED+", " +
		"."+CompileResult.STATUS_PARSE_FAILED+", " +
		"."+CompileResult.STATUS_TYPECHECK_FAILED+", " +
		"."+CompileResult.STATUS_EXCEPTION_RAISED+" { \n" +
		"	color: red;\n" +
		"}\n"+
		"."+CompileResult.STATUS_CANCEL+", " +
		"."+CompileResult.STATUS_BUG+" \n{"+
		"	color: magenta;\n" +
		"}\n";

	/**
	 * Resets the active document on the EditorPane.
	 * Will most likely destroy the old one.
	 * Sets a new root element.
	 */
	public StateViewDocument(HTMLDocument doc) {
		this.doc = doc;
		
		// Get the first otherwise valid root element:
		//   ignore Bidi stuff since we won't be generating any.
		BlockElement el = null;
		for (Element e : doc.getRootElements()) {
			if (e instanceof BlockElement) {
				el = (BlockElement) e;
				break;
			}
		}
		if (el == null) System.err.println("Couldn't find root element!");
				
		// now drill down to add our custom root element
		el = (BlockElement) getFirstChild(el, "body");
		try {
			doc.insertAfterStart(el, "<div class=\"root\"></div>");
		} catch (BadLocationException e) {
			e.printStackTrace();
		} catch (IOException e) {
			e.printStackTrace();
		}
		// retrieve that which we've just added (d'oh)
		el = (BlockElement) getFirstChild(el, "div");
		appendRoot = el;
		
		loadStyles();
	}

	/**
	 * Gets the first named child element.
	 * @param name
	 * @return
	 */
	private Element getFirstChild(Element el, String name) {
		for (int i = 0; i < el.getElementCount(); i++) {
			Element e = el.getElement(i);
			if (e.getName() == name) {
				return e;
			}
		}
		if (el == null)	System.err.println("Couldn't find matching child element "+name+".");
		return null;
	}
	
	/**
	 * Appends pure HTML crudely to the current panel.
	 * Don't think special characters are escaped.
	 * @param text the HTML to append.
	 */
	public synchronized void appendHTML(String text) {
		// initialise stuff
		try {
			doc.insertBeforeEnd(appendRoot, text);
		} catch (BadLocationException e) {
			System.err.println("BadLocation writing data to HTML panel");
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	/**
	 * Appends HTML crudely to the current panel, suffixing a newline.
	 * @param text a line of HTML to append.
	 * @param cls the class attribute to give the element.
	 */
	public void appendPar(String text, String cls) {
		text.replaceAll("\\n", "<br/>\n");
		if (cls == null || cls.equals("")) {
			cls = "";
		} else {
			cls = " class=\""+cls+"\"";
		}
		appendHTML("<p"+cls+">"+text+"</p>");
	}
	

	/**
	 * Loads styles, but does not remove existing ones: therefore this method should 
	 * probably be run only once per document.
	 */
	private void loadStyles() {
		// built-in customisation.
		doc.getStyleSheet().addRule(StyleSheet);
		
        // Add external stylesheet if defined
		String stylePath = jEdit.getProperty(PolyMLPlugin.PROPS_STATE_OUTPUT_CSS_FILE);
		if (! (stylePath == null || stylePath.equals("") )) {
			StringBuilder rules = new StringBuilder();
			BufferedReader reader;
			try {
				reader = new BufferedReader(new FileReader(PolyMLPlugin.PROPS_STATE_OUTPUT_CSS_FILE));
				String line = null;
				while (( line = reader.readLine()) != null) {
				    rules.append(line + System.getProperty("line.separator"));
				}
			} catch (FileNotFoundException e) {
				rules = null;
			} catch (IOException e) {
				rules = null;
			}
			if (rules == null) {
				System.err.println("PolyML Plugin: Specified external stylesheet not found or not read.");
			} else {
				doc.getStyleSheet().addRule(rules.toString());
			}
		}
	}

	/**
	 * Shamelessly convenience method for {@link #appendPar(String, String)}.
	 */
	public void appendPar(String msg, char c) {
		appendPar(msg, String.valueOf(c));
	}
}

/*
 *
 *  (c) COPYRIGHT INRIA and W3C, 2007
 *  Please first read the full copyright statement in file COPYRIGHT.
 *
 */

/*
 * This module contains functions to deal with javascript
 * using SpiderMonkey (Mozilla's interpreter).
 * See also javascript.h before adding new javascript objects.
 *
 * Author: F. Wang
 *
 */         

/*----------------------------------------------------------------------
 *                 Structure of the javascript tree
 *
 * The object at top level is the "global object". There is no
 * W3C-standard that defines what are its direct children but
 * the organization in Amaya has been chosen to be compatible
 * with the one of usual browsers:
 *
 *  Window (global object)
 *    |
 *    |_ Navigator
 *    |
 *    |_ Screen
 *    |
 *    |_ History
 *    |
 *    |_ Location
 *    |
 *    |_ Document
 *
 * (A detailled description can be found at
 *         http://www.w3schools.com/js/js_obj_htmldom.asp)
 * 
 * Actually, the global object has no name. Nevertheless, it
 * has a property "window" that returns itself, so everything
 * happens as if it was its name. This has consequences on the
 * javascript syntax: for example, if you want to call the
 * function confirm(), you can do confirm(), window.confirm()
 * or even window.window.window.window.confirm()
 *
 * Navigator, Screen, History and Location are objects that allow
 * to access general informations about supported features and
 * to execute several commands of Amaya.
 *
 * Document represents the structure of the current document.
 * Its organization is a W3C standard called "Document Object Model".
 * The specifications can be found at http://www.w3.org/DOM/DOMTR
 * 
  -----------------------------------------------------------------------*/

/* Included headerfiles */
#ifdef _WX
#include "wx/wx.h"
#endif /* _WX */

#undef THOT_EXPORT
#define THOT_EXPORT extern
#include "amaya.h"
#include "init_f.h"
#include "HTMLsave_f.h"
#include "HTMLbook_f.h"
#include "AHTURLTools_f.h"
#include "css.h"
#include "trans.h"
#include "undo.h"
#include "interface.h"
#include "MathML.h"
#include "parser.h"
#ifdef _SVG
#include "SVG.h"
#endif /* _SVG */
#include "document.h"

#include "MENUconf.h"
#include "EDITORactions_f.h"
#include "HTMLhistory_f.h"
#include "fetchXMLname_f.h"
#include "SVGbuilder_f.h"
#include "html2thot_f.h"
#include "HTMLtable_f.h"
#include "HTMLactions_f.h"
#include "HTMLedit_f.h"
#include "HTMLpresentation_f.h"
#include "MathMLbuilder_f.h"
#include "styleparser_f.h"
#include "trans_f.h"
#include "UIcss_f.h"
#ifdef _WX
#include "wxdialogapi_f.h"
#endif /* _WX */
#ifdef _WINGUI
#include "wininclude.h"
#endif /* _WINGUI */
#ifdef _WINDOWS
#include <commctrl.h>
#endif /* _WINDOWS */
#include "XLinkedit_f.h"

/* headerfiles and objects used by SpiderMonkey */
#include <jsapi.h>
#include "javascript.h"
static JSRuntime *grt = NULL;
static JSContext *gcx = NULL;
static JSObject *gobj;

/* Informations about the document where the script is run */
static Document jsdocument;
static View jsview;
static ThotBool withDocType;
static ThotBool ContinueScript = FALSE;
static ThotBool ConfirmDialog = FALSE;

/* Functions that deal with the scripts */
static ThotBool CheckInitJavascript (Document doc, View view);
static ThotBool InitJavascript (Document document, View view);
static ThotBool CreateDoctypeObject(JSObject *document_object);
static void ExecuteAllScripts();
static void ExecuteScripts (Element Script);
//static void ExecuteJSfile (const char *location);
static ThotBool IsJavascript(Element Script);
static void printError(JSContext *cx, const char *message, JSErrorReport *report);
//TODO: add a callbackbranch

/* Functions used for conversion between Thot/SpiderMonkey */
static jsval Element_to_jsval(JSContext *cx, Element ThotNode);
static jsval Attribute_to_jsval(JSContext *cx, Attribute ThotAttr);
static JSBool jsContinueScript();
static jsval string_to_jsval(JSContext *cx, char *string);
static char *jsval_to_string(JSContext *cx, jsval value);
static void jsval_to_ElementType (JSContext *cx, jsval name, ElementType *elType);
static void jsval_to_AttributeType (JSContext *cx, jsval name, AttributeType *attrType);

/* Other functions used by the DOM interface */
static jsval ObjectWithInfo(JSContext *cx, JSObject *obj, JSClass *jsclass, int name_id, uintN argc, jsval *argv);
static ThotBool IsElementNode(Element element);
static Element NextTypedNodeElement (ElementType elType, Element Tree, Element Start);
static int GetNodeType(JSObject *obj);
static jsval GetNodeName(JSContext *cx, JSObject *obj);
static jsval GetNodeValue(JSContext *cx, JSObject *obj);

/*----------------------------------------------------------------------
  Switch_JS_DOM
    Switch the javascript engine ON/OFF
  -----------------------------------------------------------------------*/
void Switch_JS_DOM(Document document, View view)
{
  /* Check whether a script can be executed inside the document*/
  if (DocumentTypes[document] == docSource ||
      DocumentTypes[document] == docText || DocumentTypes[document] == docLog
      || !strcmp (DocumentURLs[document], "empty")
      )
    {
    UpdateJavascriptMenus ();
    return;
    }

  if(ContinueScript && document == jsdocument)
    /* Switch OFF */
    StopJavascript (jsdocument);
  else
    {
    /* Switch ON */
    ContinueScript = CheckInitJavascript(document, view);
    UpdateJavascriptMenus ();

    if(ContinueScript)
      /* search and execute all scripts contained in jsdocument */
      ExecuteAllScripts();
    }
}

/*----------------------------------------------------------------------
  ExecuteACommand
    Display a dialog box where the user can enter a javascript command
  -----------------------------------------------------------------------*/
void ExecuteACommand(Document document, View view)
{
jsval rval;
char *rchar = NULL;

  if(ContinueScript)
    {
    #ifdef _WX
    /* Ask the user to enter a command */
    *JavascriptPromptValue = EOS;

    CreateTextDlgWX (
      BaseDialog + JavascriptPromptForm, BaseDialog + JavascriptPromptText,
      TtaGetViewFrame (document,  view), "Javascript/DOM",
      TtaGetMessage(AMAYA, AM_JAVASCRIPT_ENTER_A_COMMAND), "main();");

    TtaSetDialoguePosition ();
    TtaShowDialogue (BaseDialog + JavascriptPromptForm, FALSE);
    TtaWaitShowDialogue ();

    /* The script must be stopped. Do not evaluate the command */
    if(!ContinueScript)return;

    /* Execute the command and put its returned value to the status bar */
    JS_EvaluateScript(gcx, gobj, JavascriptPromptValue, strlen(JavascriptPromptValue) , DocumentURLs[jsdocument], 0, &rval);

    if(JSVAL_IS_VOID(rval))
      TtaSetStatus (jsdocument, 1, TtaGetMessage(AMAYA, AM_JAVASCRIPT_NO_RETURNED_VALUE), NULL);
    else
      {
      rchar = jsval_to_string(gcx, rval);
      /* TODO: handle returned value with \n */
      TtaSetStatus (jsdocument, 1, TtaGetMessage(AMAYA, AM_JAVASCRIPT_RETURNED_VALUE), rchar);
      }

    #endif /* _WX */
    }
}

/*----------------------------------------------------------------------
  ExecuteExternal
    Open a dialogue that allows the user to select a js file and
    to execute it in the document.
  -----------------------------------------------------------------------*/
void ExecuteExternal(Document document, View view)
{
  if(ContinueScript)
    {
    /* TODO */
     //ExecuteJSfile
    }
}

/*----------------------------------------------------------------------
  AddExternal
    Add a <script> inside with a src attribute pointing to
    an external js file.
  -----------------------------------------------------------------------*/
void AddExternal(Document document, View view)
{
  CreateScript (document, view, TRUE);
}

/*----------------------------------------------------------------------
  InsertScript
    Add a <script> and open the structure View
  -----------------------------------------------------------------------*/
void InsertScript(Document document, View view)
{
  CreateScript (document, view, FALSE);
}

/*----------------------------------------------------------------------
  UpdateJavascriptMenus
    Update the javascript entries:
      - stop scripts
      - direct command
      - external script
  -----------------------------------------------------------------------*/
void UpdateJavascriptMenus ()
{
Document document;

  for (document = 1; document < DocumentTableLength; document++)
    {
    if(document == jsdocument && ContinueScript)
      {
      /* javascript item ON */
      #ifdef _WX
        TtaSetToggleItem (document, 1, JavascriptDom_, BSwitch_JS_DOM, TRUE);
        TtaSetItemOn (document, 1, JavascriptDom_, BExecuteACommand);
        TtaSetItemOn (document, 1, JavascriptDom_, BExecuteExternal);
      #endif /* _WX */
      }
    else
      {
      /* javascript item OFF */
      #ifdef _WX
        TtaSetToggleItem (document, 1, JavascriptDom_, BSwitch_JS_DOM, FALSE);
        TtaSetItemOff (document, 1, JavascriptDom_, BExecuteACommand);
        TtaSetItemOff (document, 1, JavascriptDom_, BExecuteExternal);
      #endif /* _WX */
      }
    }
}

/*----------------------------------------------------------------------
  CheckInitJavascript
    - Check if no script is running
    - Check the View
    - Check if the document is saved
    - Call InitJavascript
    - Check whether an error occured
  -----------------------------------------------------------------------*/
ThotBool CheckInitJavascript(Document doc, View view)
{
if(gcx && JS_IsRunning(gcx))
  {
  /* Another script is running */
  TtaDisplayMessage(FATAL, TtaGetMessage(AMAYA, AM_JAVASCRIPT_IS_RUNNING));
  return FALSE;
  }

if(view != 1)
  {
  /* Check if script can be executed in this view */
  TtaDisplayMessage(FATAL, TtaGetMessage(AMAYA, AM_JAVASCRIPT_VIEW));
  return FALSE;
  }

/* TODO: check if there is no error in the xml source before executing a script. */

if(TtaIsDocumentModified (doc))
  {
  /* ask if the user wants to continue, save, or cancel */
  ExtraChoice = FALSE;
  UserAnswer = FALSE;
#ifdef _WX
  ConfirmError3L (doc, view, DocumentURLs[doc],
                    TtaGetMessage (AMAYA, AM_DOC_MODIFIED),
                    NULL,
                    TtaGetMessage (AMAYA, AM_DONT_SAVE),
                    TtaGetMessage (LIB, TMSG_BUTTON_SAVE));
#endif /* _WX */

      if (UserAnswer)
        {
          if (DocumentSource[doc] && !TtaIsDocumentUpdated (doc) &&
              TtaIsDocumentUpdated (DocumentSource[doc]))
            // the last updated version is the document source
            SaveDocument (DocumentSource[doc], view);
          else
            SaveDocument (doc, view);
        }
      else if (ExtraChoice)
        {
          TtaSetDocumentUnmodified (doc);
          if (DocumentSource[doc])
            TtaSetDocumentUnmodified (DocumentSource[doc]);
          /* remove the corresponding auto saved doc */
          RemoveAutoSavedDoc (doc);
        }
      else return FALSE;
  }

if(!InitJavascript (doc, view))
  {
  /* The Javascript/DOM objects could not be built */
  TtaDisplayMessage(FATAL, TtaGetMessage(AMAYA, AM_JAVASCRIPT_CAN_NOT_BUILD));
  return FALSE;
  }

return TRUE;
}

/*----------------------------------------------------------------------
  InitJavascript
  Build the javascript/DOM objects
  return TRUE if no error occured
  -----------------------------------------------------------------------*/
ThotBool InitJavascript (Document document, View view)
{
  JSObject *object, *object2;

/* initialize the JS run time */
  if(!grt)grt = JS_NewRuntime(8L * 1024L * 1024L);
  if (!grt)return FALSE;

/* (re)create a context and associate it with the JS run time
   (for the moment, you can't have several documents that are
    running javascript simultaneously) */

  if(gcx)
    {
    /* Check if no scripts/functions are running, so that gcx
       can be safely destroyed. Normally, this has already
       be checked in CheckInitJavascript  */
    if(JS_IsRunning(gcx))
      return FALSE;

    /* Destroy the context */
    JS_DestroyContext(gcx);
    }

  gcx = JS_NewContext(grt, 8192);
  if (!gcx)return FALSE;

/* Set a CallBackBranch */
/* TODO: Call TtaHandlePendingEvents so that the user can stop javascript,
   even in loops
JS_SetBranchCallback(gcx, (JSBranchCallback) ??);
*/

/* register document and view */
  jsdocument = document;
  jsview = view;

/* register error handler */
  JS_SetErrorReporter(gcx, printError);

/* create the global object (window) and the built-in JS objects */
  gobj = JS_NewObject(gcx, &window_class, NULL, NULL);
  if(!gobj)return FALSE;

  JS_InitStandardClasses(gcx, gobj);
  if(!gobj)return FALSE;
  JS_DefineFunctions(gcx, gobj, window_functions);
  JS_DefineProperties(gcx, gobj, window_properties);

/* create the object navigator */
  object = JS_DefineObject(gcx, gobj, "navigator", &navigator_class, NULL, JSPROP_READONLY);
  if(!object)return FALSE;
    JS_DefineFunctions(gcx, object, navigator_functions);
    JS_DefineProperties(gcx, object, navigator_properties);

/* create the object screen */
  object = JS_DefineObject(gcx, gobj, "screen", &screen_class, NULL, JSPROP_READONLY);
  if(!object)return FALSE;
    JS_DefineProperties(gcx, object, screen_properties);

/* create the object history */
  object = JS_DefineObject(gcx, gobj, "history", &history_class, NULL, JSPROP_READONLY);
  if(!object)return FALSE;
    JS_DefineFunctions(gcx, object, history_functions);
    JS_DefineProperties(gcx, object, history_properties);

/* create the object location */
  object = JS_DefineObject(gcx, gobj, "location", &location_class, NULL, JSPROP_READONLY);
  if(!object)return FALSE;
    JS_DefineFunctions(gcx, object, location_functions);
    JS_DefineProperties(gcx, object, location_properties);

/* create the object document */
  object = JS_DefineObject(gcx, gobj, "document", &Document_class, NULL, JSPROP_READONLY);
  if(!object)return FALSE;
    JS_DefineFunctions(gcx, object, Document_functions);
    JS_DefineFunctions(gcx, object, Node_functions);
    JS_DefineProperties(gcx, object, Node_properties);
    JS_DefineProperties(gcx, object, Document_properties);
    JS_SetPrivate(gcx, object, (void *)TtaGetMainRoot(jsdocument));

  /* create the document.doctype object */
    if(!CreateDoctypeObject(object))
      return FALSE;

  /* create the document.implementation object */
    object2 = JS_DefineObject(gcx, object, "implementation", &DOMImplementation_class, NULL, JSPROP_READONLY);
    if(!object2)return FALSE;
      JS_DefineFunctions(gcx, object2, DOMImplementation_functions);

  return TRUE;
}

/*----------------------------------------------------------------------
  CreateDoctypeObject
  -----------------------------------------------------------------------*/
static ThotBool CreateDoctypeObject(JSObject *document_object)
{
  JSObject *object;
  Element         el_doc, el_doctype;
  ElementType elType;
  char           *s;

  el_doc = TtaGetMainRoot (jsdocument);
  elType = TtaGetElementType (el_doc);
  s = TtaGetSSchemaName (elType.ElSSchema);
  if (strcmp (s, "HTML") == 0)
    elType.ElTypeNum = HTML_EL_DOCTYPE;
  else if (strcmp (s, "SVG") == 0)
    elType.ElTypeNum = SVG_EL_DOCTYPE;
  else if (strcmp (s, "MathML") == 0)
    elType.ElTypeNum = MathML_EL_DOCTYPE;
  else
    elType.ElTypeNum = XML_EL_doctype;

  el_doctype = TtaSearchTypedElement (elType, SearchInTree, el_doc);
  withDocType = (el_doctype != NULL);

  if(withDocType)
    {
    object = JS_DefineObject(gcx, document_object, "doctype", &DocumentType_class, NULL, JSPROP_READONLY);
    if(!object)return FALSE;
    JS_DefineProperties(gcx, object, DocumentType_properties);
    JS_SetPrivate(gcx, object, (void *)el_doctype);
    }

  return TRUE;
}

/*----------------------------------------------------------------------
  StopJavascript
    If a script is running in doc, register the fact that
    the execution must be stopped.
  -----------------------------------------------------------------------*/
void StopJavascript (Document doc)
{
  if(doc == jsdocument)
    {
    /* Destroy alert, confirm, "enter a command" and prompt */
    /* TODO:alert */
    if(ConfirmDialog)TtaDestroyDialogue (BaseDialog + ConfirmForm);
    TtaDestroyDialogue (BaseDialog + JavascriptPromptForm);

    /* Update data */
    ContinueScript = FALSE;
    UpdateJavascriptMenus ();
    }
}

/*----------------------------------------------------------------------
  DestroyJavascript
  Free all objects used by SpiderMonkey
  This function must be call at exit
  -----------------------------------------------------------------------*/
void DestroyJavascript ()
{
if(ContinueScript)StopJavascript (jsdocument);
if(gcx)JS_DestroyContext(gcx);
if(grt)JS_DestroyRuntime(grt);
JS_ShutDown();
}

/*----------------------------------------------------------------------
  ExecuteAllScripts
  Execute all scripts contained in the jsdocument
  -----------------------------------------------------------------------*/
static void ExecuteAllScripts()
{
Element child, child2;

  child = TtaGetMainRoot(jsdocument);

  while(child)
    {
    child2 = child;

    while(child2)
      {
      child = child2;

      if(IsJavascript(child))
        {
        ExecuteScripts (child);
        if(!ContinueScript)return;
        child2 = child;
        TtaNextSibling(&child2);
        }
      else child2 = TtaGetFirstChild(child);
      }

    child = TtaGetSuccessor(child);
    }
}

/*----------------------------------------------------------------------
  ExecuteScripts
  Execute scripts contained in an element SCRIPT_
  -----------------------------------------------------------------------*/
static void ExecuteScripts (Element Script)
{
Element child, child2, child3;
jsval rval;
const char *url = DocumentURLs[jsdocument];
char *text = NULL;
int len, i, len2, line;
Language	lang;

ElementType    elType;
AttributeType  attrType;
Attribute      attr;

/* Execute script located at the location indicated by src */
elType = TtaGetElementType (Script);
attrType.AttrSSchema = elType.ElSSchema;
attrType.AttrTypeNum = HTML_ATTR_script_src;
attr = TtaGetAttribute (Script, attrType);
if(attr)
  {
  /* TODO */
  //TtaGiveTextAttributeValue(attr, text, &len);
  if(!ContinueScript)return;
  }

/* Execute each script of (SCRIPT_)'s children
   Normally there should only be one child but it is also possible to have something like :
    SCRIPT_
      TEXT_UNIT <-- contained only spaces and '\n'
      CDATA     <-- contained the script that have to be executed
      TEXT_UNIT <-- contained only spaces and '\n'
*/

child = TtaGetFirstChild(Script);
while(child)
  {
  line = TtaGetElementLineNumber(child);

  switch(TtaGetElementType(child).ElTypeNum)
    {
    case HTML_EL_TEXT_UNIT:
       /* Execute the script contained in the TEXT_UNIT */
       len = TtaGetTextLength(child);
       text = (char *)TtaGetMemory (len + 1);
       if(text)
         {
         TtaGiveTextContent(child, (unsigned char *)text, &len, &lang);
         JS_EvaluateScript(gcx, gobj, text, len , url, line, &rval);
         }
    break;

    case HTML_EL_CDATA:
    case HTML_EL_Comment_:

      /* Compute size of script */
      len = 0;
      child2 = TtaGetFirstChild(child);
      while(child2)
        {
        child3 = TtaGetFirstChild(child2);
        if(TtaGetElementType(child3).ElTypeNum == HTML_EL_TEXT_UNIT)len+=TtaGetTextLength(child3);
        len++;
        TtaNextSibling(&child2);
        }

      text = (char *)TtaGetMemory (len + 1);

      if(text)
        {
        /* Group all the text in one string */

        child2 = TtaGetFirstChild(child);
        i = 0;
        while(child2)
          {
          child3 = TtaGetFirstChild(child2);
          if(TtaGetElementType(child3).ElTypeNum == HTML_EL_TEXT_UNIT)
            {
            len2 = TtaGetTextLength(child3);
            TtaGiveTextContent(child3, (unsigned char *)(&(text[i])), &len2, &lang);
            i+=len2;
            }
          text[i] = '\n';i++;
          TtaNextSibling(&child2);
          }

        text[i] = '\0';
      
        /* Execute the script */
        JS_EvaluateScript(gcx, gobj, text, len , url, line, &rval);
        }
    break;

    default:
    break;
    }
  if(text)TtaFreeMemory (text);

  if(!ContinueScript)return;
  TtaNextSibling(&child);
  }

}

/*----------------------------------------------------------------------
  ExecuteJSfile
  Execute the js file located at url
  -----------------------------------------------------------------------*/
/*static void ExecuteJSfile (const char *location)
{
JSScript *ScriptInFile;

         TODO

    //ScriptInFile = JS_CompileFile(gcx, gobj, text);
    //if(ScriptInFile)JS_ExecuteScript(gcx, gobj, ScriptInFile, &rval);
}*/

/*----------------------------------------------------------------------
  IsJavascript
  Check whether an element is a SCRIPT_ of type javascript
  -----------------------------------------------------------------------*/
static ThotBool IsJavascript(Element element)
{
ElementType    elType;
AttributeType  attrType;
Attribute      attr;
#define BUFLEN 20
char buffer[BUFLEN];
int len;

if(!element)return FALSE;
if(TtaGetElementType(element).ElTypeNum != HTML_EL_SCRIPT_)return FALSE;

elType = TtaGetElementType (element);
attrType.AttrSSchema = elType.ElSSchema;
attrType.AttrTypeNum = HTML_ATTR_content_type;
attr = TtaGetAttribute (element, attrType);
if(!attr)return FALSE;

len = TtaGetTextAttributeLength(attr);
if(len >= BUFLEN)return FALSE;

TtaGiveTextAttributeValue(attr, buffer, &len);
if(strcmp("text/javascript", buffer))return FALSE; else return TRUE;
}

/*----------------------------------------------------------------------
  printError
    Report javascript error
  -----------------------------------------------------------------------*/
static void printError(JSContext *cx, const char *message, JSErrorReport *report)
{
StopJavascript (jsdocument);
TtaDisplayMessage(FATAL, TtaGetMessage(AMAYA, AM_JAVASCRIPT_SCRIPT_ERROR),
   report->filename, report->lineno, message);
}

/*----------------------------------------------------------------------
  Element_to_jsval
    Convert a Thot element into a javascript object.
  -----------------------------------------------------------------------*/
static jsval Element_to_jsval(JSContext *cx, Element ThotNode)
{
int TypeOfNode;
JSClass *ClassToApply;
JSObject *jsNode;

if(!ThotNode)return JSVAL_NULL;

  /* It's not the root : analyse the type of node */
  TypeOfNode = TtaGetElementType(ThotNode).ElTypeNum;

  /* Choose the class to apply according to the type of ThotNode */
  switch(TypeOfNode)
    {
    case HTML_EL_DOCTYPE_line:
    case HTML_EL_Document_URL:
    case HTML_EL_C_Empty:
    case HTML_EL_Basic_Elem:
    case HTML_EL_C_BR:
      return JSVAL_NULL; /* These elements are not taken into account to build the DOM tree */
    break;

    case HTML_EL_DOCTYPE:
      ClassToApply = &DocumentType_class;
    break;

    case TEXT_UNIT:
      ClassToApply = &Text_class;
    break;

    case HTML_EL_Comment_:
      ClassToApply = &Comment_class;
    break;

    case HTML_EL_CDATA:
      ClassToApply = &CDATASection_class;
    break;

    default:
      ClassToApply = &Element_class;
    break;
    }

  jsNode = JS_NewObject(cx, ClassToApply, NULL, NULL);
  if(!jsNode)return JSVAL_NULL;

  /* Create its basic properties and methods (DOM Core) */
  JS_DefineProperties(cx, jsNode, Node_properties);
  JS_DefineFunctions(cx, jsNode, Node_functions);

  /* Add properties and methods according to the type of ThotNode */
  switch(TypeOfNode)
    {
    case TEXT_UNIT:
      //JS_DefineFunctions(cx, jsNode, Text_functions);
    break;

    case HTML_EL_DOCTYPE:
      JS_DefineProperties(cx, jsNode, DocumentType_properties);
    break;

    default:
      JS_DefineProperties(cx, jsNode, Element_properties);
      JS_DefineFunctions(cx, jsNode, Element_functions);
    break;
    }

  /* register ThotNode as a private data */
  JS_SetPrivate(cx, jsNode, (void *)ThotNode);

  return OBJECT_TO_JSVAL(jsNode);
}

/*----------------------------------------------------------------------
  Attribute_to_jsval
    Convert a Thot attribute into a javascript object
  -----------------------------------------------------------------------*/
static jsval Attribute_to_jsval(JSContext *cx, Attribute ThotAttr)
{
JSObject *jsAttr;

if(!ThotAttr)return JSVAL_NULL;

  jsAttr = JS_NewObject(cx, &Attr_class, NULL, NULL);
  if(!jsAttr)return JSVAL_NULL;
  
  JS_DefineProperties(cx, jsAttr, Node_properties);
  JS_DefineFunctions(cx, jsAttr, Node_functions);
  JS_DefineProperties(cx, jsAttr, Attr_properties);

  /* register ThotAttr as a private data */
  JS_SetPrivate(cx, jsAttr, (void *)ThotAttr);

  return OBJECT_TO_JSVAL(jsAttr);
}

/*----------------------------------------------------------------------
  jsContinueScript
    This function returns a JSBool version of ContinueScript
    It must be returned at each end of a javascript functions, so that
    SpiderMonkey can interrupt the execution if necessary.
  -----------------------------------------------------------------------*/
static JSBool jsContinueScript()
{
return (ContinueScript ? JS_TRUE : JS_FALSE);
}

/*----------------------------------------------------------------------
  string_to_jsval
    Convert a string to a javascript value
  -----------------------------------------------------------------------*/
static jsval string_to_jsval(JSContext *cx, char *string)
{
return STRING_TO_JSVAL(JS_NewStringCopyZ(cx, string));
}

/*----------------------------------------------------------------------
  jsval_to_string
    Convert a javascript value to a string
  -----------------------------------------------------------------------*/
static char *jsval_to_string(JSContext *cx, jsval value)
{
return (JSVAL_IS_VOID(value) ? NULL : JS_GetStringBytes(JS_ValueToString(cx, value)) );
}

/*----------------------------------------------------------------------
  jsval_to_ElementType
    Convert a javascript string coding a tag name into an ElementType
  -----------------------------------------------------------------------*/
static void jsval_to_ElementType (JSContext *cx, jsval name, ElementType *elType)
{
char*       mappedName;
char       content;
ThotBool    checkProfile;
int xmlType; /* See parser.h */

  elType->ElSSchema = 0;

  for(xmlType=XHTML_TYPE; xmlType<Template_TYPE; xmlType++)
    {
    elType->ElTypeNum = 0; 
    MapXMLElementType(xmlType, jsval_to_string(cx, name), elType, &mappedName, &content,
                      &checkProfile, jsdocument);
    if(elType->ElTypeNum)break;
    }

}

/*----------------------------------------------------------------------
  jsval_to_AttributeType
    Convert a javascript string coding an attribute name into an AttributeType
  -----------------------------------------------------------------------*/
static void jsval_to_AttributeType (JSContext *cx, jsval name, AttributeType *attrType)
{
  attrType->AttrSSchema = NULL;
  TtaGetXmlAttributeType (jsval_to_string(cx, name), attrType, jsdocument);
}

/*----------------------------------------------------------------------
  ObjectWithInfo
    Create an object of class jsclass, built by the Property/Method name_id
    and using the argc arguments pointed by argv. The parent object is obj.

    This function is used for specific objects such as NodeList or NamedNodeMap.
  -----------------------------------------------------------------------*/
static jsval ObjectWithInfo(JSContext *cx, JSObject *obj, JSClass *jsclass, int name_id, uintN argc, jsval *argv)
{
uintN i;
jsval v;

/* TODO: Rewrite and improve this function...
   Return null for the moment... */
return JSVAL_NULL;

/* Create the jsobject and an array to store information */
JSObject *jsobj = JS_NewObject(cx, jsclass, NULL, obj);
JSObject *jsarray = JS_NewArrayObject(cx, 0, NULL);

if(!jsobj || !jsarray)return JSVAL_NULL;

/* Define properties and methods according to the class */
switch(name_id)
  {
  /* NodeList */
  case NODE_CHILDNODES:
  case GETELEMENTSBYTAGNAME:
    JS_DefineProperties(cx, jsobj, NodeList_properties);
    JS_DefineFunctions(cx, jsobj, NodeList_functions);
  break;

  /* NamedNodeMap */
  case NODE_ATTRIBUTES:
  case DOCUMENTTYPE_ENTITIES:
  case DOCUMENTTYPE_NOTATIONS:
    JS_DefineProperties(cx, jsobj, NamedNodeMap_properties);
    JS_DefineFunctions(cx, jsobj, NamedNodeMap_functions);
  break;

  default:
  break;
  }
  
/* Set the jsarray as a child of jsobj */
if(JS_SetParent(cx, jsarray, jsobj) != JS_TRUE)return JSVAL_NULL;
if(JS_SetPrivate(cx, jsobj, (void *)jsarray) != JS_TRUE)return JSVAL_NULL;

/* Store the name_id in the array */ 
v = INT_TO_JSVAL(name_id);
if(JS_SetElement(cx, jsarray, 0, &v) != JS_TRUE)return JSVAL_NULL;

/* Store the arguments argv in the array */
for(i = 0; i < argc; i++)
  {
  v = argv[i];
  if(JS_SetElement(cx, jsarray, i+1, &v) != JS_TRUE)return JSVAL_NULL;
  }
return OBJECT_TO_JSVAL(jsobj);
}

/*----------------------------------------------------------------------
    IsElementNode
  -----------------------------------------------------------------------*/
static ThotBool IsElementNode(Element element)
{

if(!element)return FALSE; 

  /* Choose the class to apply according to the type of ThotNode */
  switch(TtaGetElementType(element).ElTypeNum)
    {
    case HTML_EL_DOCTYPE_line:
    case HTML_EL_Document_URL:
    case HTML_EL_C_Empty:
    case HTML_EL_Basic_Elem:
    case HTML_EL_C_BR:
    case HTML_EL_DOCTYPE:
    case TEXT_UNIT:
    case HTML_EL_Comment_:
    case HTML_EL_CDATA:
      return FALSE;
    break;

    default:
      return TRUE;
    break;
    }
}

/*----------------------------------------------------------------------
  NextTypedNodeElement
  -----------------------------------------------------------------------*/
static Element NextTypedNodeElement (ElementType elType, Element Tree, Element Start)
{
Element child = NULL;

if(Start == NULL)
  child = TtaSearchTypedElement(elType, SearchInTree, Tree);
else
  child = TtaSearchTypedElementInTree(elType, SearchForward, Tree, Start);

  while(child && !IsElementNode(child))
    child = TtaSearchTypedElementInTree(elType, SearchForward, Tree, child);
  
return child;
}

/*----------------------------------------------------------------------
  GetNodeType
  return the type of a javascript object representing a node
  -----------------------------------------------------------------------*/
static int GetNodeType(JSObject *obj)
{
char *text = (char *)(((JSClass *)JS_GetClass(obj))->name);

if(!strcmp((&Element_class)->name, text))return ELEMENT_NODE;
if(!strcmp((&Attr_class)->name, text))return ATTRIBUTE_NODE;
if(!strcmp((&Text_class)->name, text))return TEXT_NODE;
if(!strcmp((&CDATASection_class)->name, text))return CDATA_SECTION_NODE;
if(!strcmp((&EntityReference_class)->name, text))return ENTITY_REFERENCE_NODE;
if(!strcmp((&Entity_class)->name, text))return ENTITY_NODE;
if(!strcmp((&ProcessingInstruction_class)->name, text))return PROCESSING_INSTRUCTION_NODE;
if(!strcmp((&Comment_class)->name, text))return COMMENT_NODE;
if(!strcmp((&Document_class)->name, text))return DOCUMENT_NODE;
if(!strcmp((&DocumentType_class)->name, text))return DOCUMENT_TYPE_NODE;
if(!strcmp((&DocumentFragment_class)->name, text))return DOCUMENT_FRAGMENT_NODE;
if(!strcmp((&Notation_class)->name, text))return NOTATION_NODE;

/* unknown type of node */
return 0;
}

/*----------------------------------------------------------------------
  GetNodeName
  return the name of a javascript object representing a node
  -----------------------------------------------------------------------*/
static jsval GetNodeName(JSContext *cx, JSObject *obj)
{
Element element;

switch(GetNodeType(obj))
  {
  case ELEMENT_NODE:
    element = (Element)JS_GetPrivate(cx, obj);
    return string_to_jsval(cx, GetXMLElementName(TtaGetElementType(element), jsdocument) );
  break;
  case ATTRIBUTE_NODE:
  break;
  case TEXT_NODE:
    return string_to_jsval(cx, "#text");
  break;
  case CDATA_SECTION_NODE:
    return string_to_jsval(cx, "#cdata-section");
  break;
  case ENTITY_REFERENCE_NODE:
  break;
  case ENTITY_NODE:
  break;
  case PROCESSING_INSTRUCTION_NODE:
  break;
  case COMMENT_NODE:
    return string_to_jsval(cx, "#comment");
  break;
  case DOCUMENT_NODE:
    return string_to_jsval(cx, "#document");
  break;
  case DOCUMENT_TYPE_NODE:
  break;
  case DOCUMENT_FRAGMENT_NODE:
    return string_to_jsval(cx, "#document-fragment");
  break;
  case NOTATION_NODE:
  break;
  default:
  break;
  }
return JSVAL_NULL;
}


/*----------------------------------------------------------------------
  GetNodeValue
  return the value of a javascript object representing a node
  -----------------------------------------------------------------------*/
static jsval GetNodeValue(JSContext *cx, JSObject *obj)
{
jsval name;
char *text = NULL;
Attribute attr;
Element element;
int len;
Language	lang;

switch(GetNodeType(obj))
  {
  case ATTRIBUTE_NODE:
    attr = (Attribute)JS_GetPrivate(cx, obj);
    len = TtaGetTextAttributeLength(attr);
    text = (char *)TtaGetMemory (len + 1);
    if(text)
      {
      TtaGiveTextAttributeValue(attr, (char *)text, &len);
      name = string_to_jsval(cx, text);
      TtaFreeMemory(text);
      return name;
      }
  break;
  case TEXT_NODE:
    element = (Element)JS_GetPrivate(cx, obj);
    len = TtaGetTextLength(element);
    text = (char *)TtaGetMemory (len + 1);
    if(text)
      {
      TtaGiveTextContent(element, (unsigned char *)text, &len, &lang);
      name = string_to_jsval(cx, text);
      TtaFreeMemory(text);
      return name;
      }
  break;
  case CDATA_SECTION_NODE:
  break;
  case PROCESSING_INSTRUCTION_NODE:
  break;
  case COMMENT_NODE:
  break;
  default:
  break;
  }
return JSVAL_NULL;
}

/*----------------------------------------------------------------------
  setProperty
  -----------------------------------------------------------------------*/
static JSBool setProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
char *urlname;

if(JSVAL_IS_INT(id))
  {
  switch (JSVAL_TO_INT(id))
    {
    case LOCATION_HREF:
      urlname = jsval_to_string(cx, *vp);
      if(urlname)
        {
        GetAmayaDoc (urlname, NULL, jsdocument, jsdocument, CE_ABSOLUTE, TRUE, NULL, NULL);
        return JS_FALSE;
        }
    break;

    default:
    break;
    }
  }
return jsContinueScript();
}

/*----------------------------------------------------------------------
  getProperty
  -----------------------------------------------------------------------*/
static JSBool getProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
Element element, child;
ElementType    elType;
char *text;
int n, width, height;
JSObject *array;
jsval array_element;

if(JSVAL_IS_INT(id))
  {
  switch (JSVAL_TO_INT(id))
    {
    case WINDOW:
    case WINDOW_FRAMES:
    case WINDOW_OPENER:
    case WINDOW_SELF:
    case WINDOW_TOP:
    case WINDOW_PARENT:
      *vp = OBJECT_TO_JSVAL(gobj);
    break;
    case WINDOW_CLOSED: *vp = JSVAL_FALSE; break;
    case WINDOW_LENGTH: *vp = JSVAL_ZERO; break;

    case NAVIGATOR_BROWSERLANGUAGE:
      *vp = string_to_jsval(cx, TtaGetLanguageName(TtaGetDefaultLanguage()));
    break;

    case NAVIGATOR_APPNAME:
    case NAVIGATOR_USERAGENT:
      *vp = string_to_jsval(cx, "Amaya");
    break;

    case NAVIGATOR_APPCODENAME:
      *vp = string_to_jsval(cx, "The W3C's editor/browser");
    break;

    case NAVIGATOR_APPVERSION:
      *vp = string_to_jsval(cx, "9.56");
    break;

    case NAVIGATOR_PLATFORM:
       #ifdef _WINDOWS
          *vp = string_to_jsval(cx, "Windows");
       #else
          #ifdef _MACOS
          *vp = string_to_jsval(cx, "MacOS");
          #else
            #ifdef _UNIX
               *vp = string_to_jsval(cx, "Unix");
            #endif
          #endif
       #endif
    break;

    case NAVIGATOR_COOKIESENABLED:
      *vp = JSVAL_FALSE;
    break;

    case SCREEN_PIXELDEPTH:
    case SCREEN_COLORDEPTH:
      *vp = INT_TO_JSVAL(TtaGetScreenDepth());
    break;

    /* TODO : WIDTH and HEIGHT must be for the whole screen */
    case SCREEN_AVAILWIDTH:
    case SCREEN_WIDTH:
      TtaGiveWindowSize(jsdocument, jsview, UnPixel, &width, &height);
      *vp = INT_TO_JSVAL(width);
    break;

    case SCREEN_AVAILHEIGHT:
    case SCREEN_HEIGHT:
      TtaGiveWindowSize(jsdocument, jsview, UnPixel, &width, &height);
      *vp = INT_TO_JSVAL(height);
    break;

    case LOCATION_HREF:
    case LOCATION_HASH:
      /* TODO: for href, return the complete url (with #xxx) */
      *vp = string_to_jsval(cx, DocumentURLs[jsdocument]);
    break;

    case LOCATION_PATHNAME:
      text = GetBaseURL (jsdocument);
      if(text)
        {
        *vp = string_to_jsval(cx, text);
        TtaFreeMemory (text);
        }
    break;

    case DOCUMENT_DOCUMENTELEMENT:
      element = TtaGetMainRoot (jsdocument);
      elType = TtaGetElementType (element);
      elType.ElTypeNum = HTML_EL_HTML;
      child = TtaSearchTypedElement (elType, SearchInTree, element);
      if(!child)
        /* return the main root */
        *vp = Element_to_jsval(cx, element);
      else
        /* return the HTML tag */
        *vp = Element_to_jsval(cx, child);

      /* TODO: add SVG, MathML roots... */

    break;

    case DOCUMENT_DOCTYPE:
      /* If the document has no doctype, return null */
      if(!withDocType)
        *vp = JSVAL_NULL;
    break;

    case DOCUMENTTYPE_NAME:
      elType = TtaGetElementType (TtaGetMainRoot (jsdocument));
      text = TtaGetSSchemaName (elType.ElSSchema);
      *vp = string_to_jsval(cx, text);
    break;

    case ELEMENT_TAGNAME:
      *vp = GetNodeName(cx, obj);
    break;

    case NODE_NODETYPE:
      *vp = INT_TO_JSVAL(GetNodeType(obj));
    break;

    case NODE_NODENAME:
      *vp = GetNodeName(cx, obj);
    break;

    case NODE_NODEVALUE:
      *vp = GetNodeValue(cx, obj);
    break;

    case NODE_ATTRIBUTES:
      if(GetNodeType(obj) == ELEMENT_NODE)
        *vp = ObjectWithInfo(cx, obj, &NamedNodeMap_class, NODE_ATTRIBUTES, 0, NULL);
      else *vp = JSVAL_NULL;
    break;

    case NODE_CHILDNODES:
      *vp = ObjectWithInfo(cx, obj, &NodeList_class, NODE_CHILDNODES, 0, NULL);
    break;

    case NODE_PARENTNODE:
      switch(GetNodeType(obj))
        {
        case ATTRIBUTE_NODE:
        case ENTITY_NODE:
        case NOTATION_NODE:
          *vp = JSVAL_NULL;
        break;

        default:
          element = (Element)JS_GetPrivate(cx, obj);
          element = TtaGetParent(element);
          *vp = Element_to_jsval(cx, element);
        break;
        }
    break;

    case NODE_FIRSTCHILD:
      if(GetNodeType(obj) == ATTRIBUTE_NODE)
        {
        /* TODO */
        *vp = JSVAL_NULL;
        }
      else
        {
        element = (Element)JS_GetPrivate(cx, obj);
        element = TtaGetFirstChild(element);
        *vp = Element_to_jsval(cx, element);
        }
    break;

    case NODE_LASTCHILD:
      if(GetNodeType(obj) == ATTRIBUTE_NODE)
        {
        /* TODO */
        *vp = JSVAL_NULL;
        }
      else
        {
        element = (Element)JS_GetPrivate(cx, obj);
        element = TtaGetLastChild(element);
        *vp = Element_to_jsval(cx, element);
        }
    break;

    case NODE_PREVIOUSSIBLING:
      if(GetNodeType(obj) == ATTRIBUTE_NODE)
        *vp = JSVAL_NULL;
      else
        {
        element = (Element)JS_GetPrivate(cx, obj);
        TtaPreviousSibling(&element);
        *vp = Element_to_jsval(cx, element);
        }
    break;

    case NODE_NEXTSIBLING:
      element = (Element)JS_GetPrivate(cx, obj);
      TtaNextSibling(&element);
      *vp = Element_to_jsval(cx, element);
    break;

    case NODE_OWNERDOCUMENT:
      element = (Element)JS_GetPrivate(cx, obj);
      if(TtaGetParent(element))
        {
        element = TtaGetMainRoot(jsdocument);
        *vp = Element_to_jsval(cx, element);
        }
      else
        *vp = JSVAL_NULL;
    break;

    case NODELIST_LENGTH:
      array = (JSObject *)JS_GetPrivate(cx, obj);

      JS_GetElement(cx, array, 0, &array_element);
      element = (Element)JS_GetPrivate(cx, JS_GetParent(cx, obj));

      switch(JSVAL_TO_INT(array_element))
        {
        case NODE_CHILDNODES:
          element = TtaGetFirstChild(element);
          for(n = 0; element != NULL ; n++)TtaNextSibling(&element);
        break;
 
        case GETELEMENTSBYTAGNAME:
          JS_GetElement(cx, array, 1, &array_element);

          /* Check whether we have to match all the element or a particular type */
          if(!strcmp("*", jsval_to_string(cx, array_element) ))
            {
            elType.ElSSchema = NULL;
            elType.ElTypeNum = 0;
            }
          else
            {
            /* TODO? If elType.ElTypeNum = 0; i.e. unknown tagname */
            jsval_to_ElementType (cx, array_element, &elType);
            }

          child = NextTypedNodeElement (elType, element, NULL);
        
          for(n = 0; child != NULL ; n++)
            child = NextTypedNodeElement (elType, element, child);
        break;
        }

      *vp = INT_TO_JSVAL(n);
    break;

    default:
    break;
    }
  }
return jsContinueScript();
}

/*----------------------------------------------------------------------
  Object Window
  -----------------------------------------------------------------------*/

static JSBool window_alert(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  char *msg = NULL;

  if(JSVAL_IS_VOID(*argv))
    {
    JS_ReportError(gcx, "Missing mandatory argument 1 of function alert");
    return JS_FALSE;
    }
  else
    {
    msg = jsval_to_string(cx, *argv);
    /* TODO: make a non-modal window, to allow javascript engine to be switch OFF */
    TtaDisplayMessage(INFO, msg);

    return jsContinueScript();
    }
}

static JSBool window_confirm(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  char *msg = jsval_to_string(cx, *argv);
  ConfirmDialog = TRUE;
  InitConfirm (jsdocument, jsview, msg);
  ConfirmDialog = FALSE;
  *rval = BOOLEAN_TO_JSVAL(UserAnswer);

  return jsContinueScript();
}

static JSBool window_prompt(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  char *msg = jsval_to_string(cx, argv[0]);
  char *value = jsval_to_string(cx, argv[1]);

#ifdef _WX
  *JavascriptPromptValue = EOS;
  CreateTextDlgWX (BaseDialog + JavascriptPromptForm, BaseDialog + JavascriptPromptText, TtaGetViewFrame (jsdocument, jsview), "Prompt", msg, value);
  TtaSetDialoguePosition ();
  TtaShowDialogue (BaseDialog + JavascriptPromptForm, FALSE);
  TtaWaitShowDialogue ();
  if(!ContinueScript)return JS_FALSE;

  *rval = string_to_jsval(cx, JavascriptPromptValue);
#endif /* _WX */

  return jsContinueScript();
}

static JSBool window_close(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  AmayaClose(jsdocument, jsview);
  return jsContinueScript();
}

static JSBool window_blur(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  TtaUnselect(jsdocument);
  return jsContinueScript();
}

static JSBool window_print(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  SetupAndPrint(jsdocument, jsview);
  return jsContinueScript();
}


/*----------------------------------------------------------------------
  Object Navigator
  -----------------------------------------------------------------------*/
static JSBool navigator_javaEnabled(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
*rval = JSVAL_FALSE;
return jsContinueScript();
}

static JSBool navigator_taintEnabled(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
*rval = JSVAL_FALSE;
return jsContinueScript();
}


/*----------------------------------------------------------------------
  Object Screen
  -----------------------------------------------------------------------*/

/*----------------------------------------------------------------------
  Object History
  -----------------------------------------------------------------------*/
static JSBool history_back(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
GotoPreviousHTML (jsdocument, jsview);
return JS_FALSE;
}

static JSBool history_forward(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
GotoNextHTML (jsdocument, jsview);
return JS_FALSE;
}

/*----------------------------------------------------------------------
  Object Location
  -----------------------------------------------------------------------*/
/* TODO: search what is the difference between assign and replace... */

static JSBool location_assign(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
char *urlname = jsval_to_string(cx, *argv);
if(urlname)
  {
  GetAmayaDoc (urlname, NULL, jsdocument, jsdocument, CE_ABSOLUTE, TRUE, NULL, NULL);
  return JS_FALSE;
  }
return jsContinueScript();
}

static JSBool location_replace(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
char *urlname = jsval_to_string(cx, *argv);
if(urlname)
  {
  GetAmayaDoc (urlname, NULL, jsdocument, jsdocument, CE_ABSOLUTE, TRUE, NULL, NULL);
  return JS_FALSE;
  }
return jsContinueScript();
}

static JSBool location_reload(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
Reload(jsdocument, jsview);
return JS_FALSE;
}

/*----------------------------------------------------------------------
  Object Document
  -----------------------------------------------------------------------*/
static JSBool _getElementsByTagName(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  *rval = ObjectWithInfo(cx, obj, &NodeList_class, GETELEMENTSBYTAGNAME, argc, argv);
  return jsContinueScript();
}

/*----------------------------------------------------------------------
  Object DOMImplementation
  -----------------------------------------------------------------------*/
static JSBool DOMImplementation_hasFeature(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  char *feature	= NULL;
  char *version	= NULL;

  if(JSVAL_IS_VOID(*argv))
    {
    JS_ReportError(gcx, "Missing mandatory argument 1 of function hasFeature");
    return JS_FALSE;
    }
  else
    {
    /* TODO : to uppercase */
    feature = jsval_to_string(cx, argv[0]);
    version = jsval_to_string(cx, argv[1]);
    *rval = BOOLEAN_TO_JSVAL((/*!strcmp(feature, "HTML") ||*/ !strcmp(feature, "XML")) && (version == NULL || !strcmp(version, "1.0")) );
    return jsContinueScript();
    }
}

/*----------------------------------------------------------------------
  Object Node
  -----------------------------------------------------------------------*/
/*
static JSBool Node_createElement(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Node_replaceChild(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Node_removeChild(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Node_appendChild(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);*/

static JSBool Node_hasChildNodes(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  Element element;
  element = (Element)JS_GetPrivate(cx, obj);
  element = TtaGetFirstChild(element);
  *rval = BOOLEAN_TO_JSVAL(Element_to_jsval(cx, element) != JSVAL_NULL);
  return jsContinueScript();
}

//static JSBool Node_cloneNode(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);*/

/*----------------------------------------------------------------------
  Object NodeList
  -----------------------------------------------------------------------*/
static JSBool NodeList_item(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
Element element, child;
ElementType elType;
int n;
JSObject *array;
jsval array_element;

  /* get the element from which we got the NodeList */
  element = (Element)JS_GetPrivate(cx, JS_GetParent(cx, obj));

  array = (JSObject *)JS_GetPrivate(cx, obj);
  JS_GetElement(cx, array, 0, &array_element);
  n = JSVAL_TO_INT(argv[0]);

  switch(JSVAL_TO_INT(array_element))
    {
    case NODE_CHILDNODES:
      element = TtaGetFirstChild(element);
      for( ; n > 0 && element != NULL ; n--)TtaNextSibling(&element);
      *rval = Element_to_jsval(cx, element);
    break;

    case GETELEMENTSBYTAGNAME:
      JS_GetElement(cx, array, 1, &array_element);

      /* Check whether we have to match all the element or a particular type */
      if(!strcmp("*", jsval_to_string(cx, array_element) ))
        {
        elType.ElSSchema = NULL;
        elType.ElTypeNum = 0;
        }
      else
        {
        /* TODO? If elType.ElTypeNum = 0; i.e. unknown tagname */
        jsval_to_ElementType (cx, array_element, &elType);
        }

      child = NextTypedNodeElement (elType, element, NULL);

      for( ; n > 0 && child != NULL ; n--)
        child = NextTypedNodeElement (elType, element, child);

      *rval = Element_to_jsval(cx, child);
    break;
    }

  return jsContinueScript();
}

/*----------------------------------------------------------------------
  Object NamedNodeMap
  -----------------------------------------------------------------------*/
static JSBool NamedNodeMap_getNamedItem(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
Element element;
AttributeType attrType;
Attribute      attr;

JSObject *array;
jsval array_element;

  /* get the element from which we got the NamedNodeMap */
  element = (Element)JS_GetPrivate(cx, JS_GetParent(cx, obj));

  array = (JSObject *)JS_GetPrivate(cx, obj);
  JS_GetElement(cx, array, 0, &array_element);
 
  switch(JSVAL_TO_INT(array_element))
    {
    case NODE_ATTRIBUTES:
      jsval_to_AttributeType(cx, argv[0], &attrType);
      attr = TtaGetAttribute (element, attrType);
      *rval = Attribute_to_jsval(cx, attr);
    break;

    case DOCUMENTTYPE_ENTITIES:
    case DOCUMENTTYPE_NOTATIONS:
      /* TODO */
      *rval = JSVAL_NULL;
    break;
    }

  return jsContinueScript();
}

/*----------------------------------------------------------------------
  Object Element
  -----------------------------------------------------------------------*/
/*
static JSBool Element_getAttribute(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Element_setAttribute(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Element_removeAttribute(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Element_getAttributeNode(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Element_setAttributeNode(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Element_removeAttributeNode(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool Element_normalize(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);*/
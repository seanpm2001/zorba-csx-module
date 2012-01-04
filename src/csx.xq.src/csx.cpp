#include <zorba/item_factory.h>
#include <zorba/singleton_item_sequence.h>
#include <zorba/vector_item_sequence.h>
#include <zorba/item.h>
#include <zorba/diagnostic_list.h>
#include <zorba/empty_sequence.h>
#include <zorba/store_manager.h>
#include <zorba/zorba_exception.h>
#include <zorba/store_consts.h>
#include <zorba/base64.h>

#include <string.h>
#include <stdio.h>
#include <iostream>

#include "csx.h"

namespace zorba { namespace csx {

  using namespace std;

  const String CSXModule::CSX_MODULE_NAMESPACE = "http://www.zorba-xquery.com/modules/csx";

  /*******************************************************************************************
  *******************************************************************************************/
  
  zorba::ExternalFunction*
  CSXModule::getExternalFunction(const zorba::String& localName) {
    if(localName == "parse"){
      if(!theParseFunction){
        theParseFunction = new ParseFunction(this);
      } 
      return theParseFunction;
    } else if(localName == "serialize"){
      if(!theSerializeFunction){
        theSerializeFunction = new SerializeFunction(this);
      }
      return theSerializeFunction;
    }
    return NULL;
  }

  void CSXModule::destroy() 
  {
    delete this;
  }

  CSXModule::~CSXModule()
  {
    if(!theParseFunction)
      delete theParseFunction;
    if(!theSerializeFunction)
      delete theSerializeFunction;
  }

  /*******************************************************************************************
  *******************************************************************************************/

  string getKindAsString(int kind){
    string sKind = "Unknown";
    switch(kind){
      case zorba::store::StoreConsts::attributeNode:
        sKind = "Attribute";
        break;
      case zorba::store::StoreConsts::commentNode:
        sKind = "Comment";
        break;
      case zorba::store::StoreConsts::documentNode:
        sKind = "Document";
        break;
      case zorba::store::StoreConsts::elementNode:
        sKind = "Element";
        break;
      case zorba::store::StoreConsts::textNode:
        sKind = "Text";
        break;
      case zorba::store::StoreConsts::piNode:
        sKind = "Pi";
        break;
      case zorba::store::StoreConsts::anyNode:
        sKind = "Any";
        break;
    }
    return sKind;
  }

  void traverse(Iterator_t iter, opencsx::CSXHandler* handler){
    // given a iterator transverse the item()* tree
    Item name, item, attr, aName;
    Iterator_t children, attrs;
    // here comes the magic :)
    iter->open();
    while(iter->next(item)){
      if(item.isNode()){
        int kind = item.getNodeKind();
        //cout << "node type: " << getKindAsString(kind) << endl;
        if(kind == zorba::store::StoreConsts::elementNode){
          // Map local namespace bindings to CSXHandler form
          Item::NsBindings bindings;
          item.getNamespaceBindings(bindings,
                                    zorba::store::StoreConsts::ONLY_LOCAL_NAMESPACES);
          Item::NsBindings::const_iterator ite = bindings.begin();
          Item::NsBindings::const_iterator end = bindings.end();
          opencsx::CSXHandler::NsBindings csxbindings;
          for (; ite != end; ++ite) {
            csxbindings.push_back(std::pair<std::string,std::string>
                                  (ite->first.str(), ite->second.str()));
          }

          //cout << "<StartElement>" << endl;
          item.getNodeName(name);
          //cout << "name: " << name.getNamespace() << "::" << name.getPrefix() << "::" << name.getLocalName() << endl;
          handler->startElement(name.getNamespace().str(), name.getLocalName().str(),
                                name.getPrefix().str(), &csxbindings);

          // go thru attributes
          attrs = item.getAttributes();
          if(!attrs.isNull()){
            attrs->open();
            while(attrs->next(attr)){
              kind = attr.getNodeKind();
              //cout << "attr type: " << getKindAsString(kind) << endl;
              attr.getNodeName(aName);
              //cout << "attr name: " << aName.getNamespace() << "::" << aName.getPrefix() << "::" << aName.getLocalName() << endl;
              //cout << "attr text: " << attr.getStringValue() << endl;
              handler->attribute(aName.getNamespace().str(), aName.getLocalName().str(),
                                 aName.getPrefix().str(), attr.getStringValue().str());
            }
          }

          children = item.getChildren();
          if(!children.isNull()){
            // there is at least one child
            traverse(children, handler);
          }

          //cout << "<EndElement>" << endl;
          handler->endElement(name.getNamespace().str(), name.getLocalName().str(),
                              name.getPrefix().str());
        }
        else if (kind == zorba::store::StoreConsts::textNode) {
          //cout << "<Characters>" << endl;
          handler->characters(item.getStringValue().str());
          children = NULL;
          //cout << "text content: " << item.getStringValue() << endl;
        }
        else {
          assert(false);
        }
      }
    }
    // here ends the magic :(
    iter->close();
  }

  zorba::ItemSequence_t
    SerializeFunction::evaluate(
      const Arguments_t& aArgs,
      const zorba::StaticContext* aSctx,
      const zorba::DynamicContext* aDctx) const 
  {
    void *lStore = zorba::StoreManager::getStore();
    Zorba* lZorba = Zorba::getInstance(lStore);
    stringstream theOutputStream;
    opencsx::CSXHandler* csxHandler = theProcessor->createSerializer(theOutputStream);

    try {
      //cout << "<StartDocument>" << endl;
      csxHandler->startDocument();
      for(int i=0; i<aArgs.size(); i++){
        Iterator_t iter = aArgs[i]->getIterator();
        traverse(iter, csxHandler);
      }
      //cout << "<EndDocument>" << endl;
      csxHandler->endDocument();
    } catch(ZorbaException ze){
      cerr << ze << endl;
    }

    //cout << ">>>>Returning (no Base64): " << theOutputStream << endl;
    string base64Result = zorba::encoding::Base64::encode(theOutputStream.str()).str();
    return ItemSequence_t(
      new SingletonItemSequence(Zorba::getInstance(0)->getItemFactory()->createBase64Binary(base64Result.c_str(),base64Result.length()))
    );
  }


/*******************************************************************************************
  *******************************************************************************************/
  zorba::ItemSequence_t
    ParseFunction::evaluate(
      const Arguments_t& aArgs,
      const zorba::StaticContext* aSctx,
      const zorba::DynamicContext* aDctx) const 
  {
    auto_ptr<CSXParserHandler> parserHandler(new CSXParserHandler());

    try {
      for(int i=0; i<aArgs.size(); i++){
        Iterator_t iter = aArgs[i]->getIterator();
        iter->open();
        Item input;
        if(iter->next(input)){
          string iString = zorba::encoding::Base64::decode(input.getStringValue()).str();
          stringstream ss(iString);
          parserHandler->startDocument();
          theProcessor->parse(ss, parserHandler.get());
          input.close();
        }
        iter->close();
      }
    } catch(ZorbaException ze){
      cerr << ze << endl;
    }

    vector<Item> v = parserHandler->getVectorItem();
    VectorItemSequence *vSeq = (v.size() > 0)?new VectorItemSequence(v):NULL;
    parserHandler->endDocument();
    return ItemSequence_t(vSeq);
  }

  /*** Start the CSXParserHandler implementation ***/

  CSXParserHandler::CSXParserHandler(void){
    m_defaultType = Zorba::getInstance(0)->getItemFactory()->createQName(zorba::String("http://www.w3.org/2001/XMLSchema"),zorba::String("untyped"));
    m_defaultAttrType = Zorba::getInstance(0)->getItemFactory()->createQName(zorba::String("http://www.w3.org/2001/XMLSchema"),zorba::String("AnyAtomicType"));
  }

  void CSXParserHandler::startDocument(){
    //m_emptyItem = Zorba::getInstance(0)
  }

  void CSXParserHandler::endDocument(){
    for(int i=0; i<m_itemStack.size(); i++){
      m_itemStack[i].close();
    }
    m_itemStack.clear();
  }

  void printVector(vector<pair<zorba::String, zorba::String> > v){
    cout << "[";
    for(vector<pair<zorba::String, zorba::String> >::iterator it = v.begin(); it != v.end(); it++){
      cout << "[" << it->first << ", " << it->second << "]" << endl;
    }
    cout << "]" << endl;
  }

  void CSXParserHandler::startElement(const string &uri, const string &localname,
                                      const string &prefix,
                                      const opencsx::CSXHandler::NsBindings *bindings){
    zorba::String zUri = zorba::String(uri);
    zorba::String zLocalName = zorba::String(localname);
    zorba::String zPrefix = zorba::String(prefix);
    Zorba* z = Zorba::getInstance(0);
    //cout << "nodename composites: zUri: " << zUri << " zPrefix: " << zPrefix << " zLocalName: " << zLocalName << endl;
    Item nodeName = z->getItemFactory()->createQName(zUri, zPrefix, zLocalName);

    opencsx::CSXHandler::NsBindings::const_iterator ite = bindings->begin();
    opencsx::CSXHandler::NsBindings::const_iterator end = bindings->end();
    Item::NsBindings itembindings;
    for (; ite != end; ++ite) {
      itembindings.push_back(pair<zorba::String,zorba::String>
                             (zorba::String(ite->first), zorba::String(ite->second)));
    }

    Item parent = m_itemStack.empty() ? m_parent : m_itemStack.back();
    Item thisNode;
    thisNode = z->getItemFactory()->createElementNode(parent, nodeName,
                                                      m_defaultType, false, false,
                                                      itembindings);
    m_itemStack.push_back(thisNode);
  }

  void CSXParserHandler::endElement(const string &uri, const string &localname, const string &qname){
    if(!m_itemStack.back().getParent().isNull())
      m_itemStack.pop_back();
  }

  void CSXParserHandler::characters(const string &chars){
    // create text node
    Zorba::getInstance(0)->getItemFactory()->createTextNode(m_itemStack.back(), chars);
  }

  void CSXParserHandler::atomicValue(const opencsx::AtomicType &type,
                                     const opencsx::AtomicValue &value) {
    // QQQ unimplemented
    assert(false);
  }

  void CSXParserHandler::attribute(const string &uri, const string &localname,
                                   const string &prefix, const string &value){
    zorba::String zAttrName = zorba::String(localname);
    zorba::String zUri = zorba::String(uri);
    zorba::String zPrefix = zorba::String(prefix);
    Zorba* z = Zorba::getInstance(0);
    Item nodeName = z->getItemFactory()->createQName(zUri, zPrefix, zAttrName);
    Item attrNodeValue = z->getItemFactory()->createString(zorba::String(value));
    z->getItemFactory()->createAttributeNode(m_itemStack.back(), nodeName, m_defaultAttrType, attrNodeValue);
  }

  void CSXParserHandler::attribute(const string &uri, const string &localname,
                                   const string &qname, const opencsx::AtomicType &type,
                                   const opencsx::AtomicValue &value) {
    // QQQ unimplemented
    assert(false);
  }

  void CSXParserHandler::processingInstruction(const string &target, const string &data){
    cout << "inside processinginstruction(" << target << ", " << data << ")" << endl;
    // No need to implement
  }

  void CSXParserHandler::comment(const string &chars){
    cout << "inside comment(" << chars << ")" << endl;
    // No need to implement
  }

  vector<Item> CSXParserHandler::getVectorItem(){
    return m_itemStack;
  }

  CSXParserHandler::~CSXParserHandler(){
    m_itemStack.clear();
  }
 
}/*namespace csx*/ }/*namespace zorba*/

#ifdef WIN32
#  define DLL_EXPORT __declspec(dllexport)
#else
#  define DLL_EXPORT __attribute__ ((visibility("default")))
#endif

extern "C" DLL_EXPORT zorba::ExternalModule* createModule() {
  return new zorba::csx::CSXModule();
}

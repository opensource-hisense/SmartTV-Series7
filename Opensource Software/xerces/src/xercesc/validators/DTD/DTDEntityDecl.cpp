/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * $Id: //DTV/MP_BR/DTV_X_IDTV1401_002306_12_001_37_007/l-pdk/device/mediatek_common/vm_linux/oss/source/xerces/src/xercesc/validators/DTD/DTDEntityDecl.cpp#1 $
 */


// ---------------------------------------------------------------------------
//  Includes
// ---------------------------------------------------------------------------
#include <xercesc/validators/DTD/DTDEntityDecl.hpp>

XERCES_CPP_NAMESPACE_BEGIN

/***
 * Support for Serialization/De-serialization
 ***/

IMPL_XSERIALIZABLE_TOCREATE(DTDEntityDecl)

void DTDEntityDecl::serialize(XSerializeEngine& serEng)
{
    XMLEntityDecl::serialize(serEng);

    if (serEng.isStoring())
    {
        serEng<<fDeclaredInIntSubset;
        serEng<<fIsParameter;
        serEng<<fIsSpecialChar;
    }
    else
    {
        serEng>>fDeclaredInIntSubset;
        serEng>>fIsParameter;
        serEng>>fIsSpecialChar;
    }
}

XERCES_CPP_NAMESPACE_END

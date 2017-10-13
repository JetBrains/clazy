/*
    This file is part of the clazy static checker.

    Copyright (C) 2017 Sergio Martins <smartins@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.    If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "autoconnect-slot.h"
#include "QtUtils.h"
#include "checkmanager.h"
#include "ClazyContext.h"
#include "StmtBodyRange.h"
#include "AccessSpecifierManager.h"
#include "FixItUtils.h"
#include "StringUtils.h"

#include <regex>

enum Fixit
{
    FixitNone = 0,
    FixItConnects = 1
};

AutoConnectSlot::AutoConnectSlot(const std::string &name, ClazyContext *context)
    : CheckBase(name, context)
{
    context->enableAccessSpecifierManager();
}

void AutoConnectSlot::VisitDecl(clang::Decl *decl)
{
    AccessSpecifierManager *a = m_context->accessSpecifierManager;
    if (!a)
        return;

    auto method = clang::dyn_cast<clang::CXXMethodDecl>(decl);
    if (!method)
        return;

    if (!isFixitEnabled(FixItConnects) && method->isThisDeclarationADefinition() && !method->hasInlineBody()) // Don't warn twice
        return;

    QtAccessSpecifierType specifierType = a->qtAccessSpecifierType(method);

    if (specifierType != QtAccessSpecifier_Slot)
        return;

    std::string name = method->getNameAsString();

    static std::regex rx(R"(on_(.*)_(.*))");
    std::smatch match;
    if (!regex_match(name, match, rx))
        return;

    std::string objectName = match[1].str();
    std::string signalName = match[2].str();

    clang::CXXRecordDecl *record = method->getParent();
    if (clang::FieldDecl *field = getClassMember(record, objectName)) {
        clang::QualType type = field->getType();
        if (QtUtils::isQObject(type)) {
            std::string objectTypeName = StringUtils::simpleTypeName(TypeUtils::pointeeQualType(type), lo());

            std::vector<clang::FixItHint> fixits;
            if (isFixitEnabled(FixItConnects))
                fixIts(objectName, signalName, objectTypeName, record, method, fixits);

            emitWarning(decl->getLocStart(), "Use of autoconnected UI slot: " + name, fixits);
        }
    }
}

void AutoConnectSlot::fixIts(const std::string &objectName, const std::string &signalName,
                                                         const std::string &objectTypeName,
                                                         clang::CXXRecordDecl *record, clang::CXXMethodDecl *method, std::vector<clang::FixItHint> &fixits)
{
    std::string newName = objectName + "_" + signalName;

    clang::FixItHint fixit = FixItUtils::createReplacement(method->getNameInfo().getLoc(), newName);
    fixits.push_back(fixit);

    // don't add duplicate connects
    if (!(method->isThisDeclarationADefinition() && !method->hasInlineBody())) {
        std::string newConnect = "connect( " + objectName + ", &" + objectTypeName + "::" + signalName + ", this, &" + record->getNameAsString() + "::" + newName + " );";
        for (clang::CXXMethodDecl *baseMethod : record->methods()) {
            StmtBodyRange bodyRange(baseMethod->getBody());
            if (clang::CallExpr *setupCall = findSetupUi(bodyRange)) {
                // hack    - use getLocWithOffset to skip ");"
                clang::FixItHint connectHint = FixItUtils::createInsertion(setupCall->getLocEnd().getLocWithOffset(2), "\n" + newConnect);
                fixits.push_back(connectHint);
            }
        }
    }
}

clang::CallExpr *AutoConnectSlot::findSetupUi(const StmtBodyRange &bodyRange)
{
    if (!bodyRange.isValid())
        return nullptr;

    clang::Stmt *body = bodyRange.body;
    std::vector<clang::CallExpr *> callExprs;
    HierarchyUtils::getChilds<clang::CallExpr>(body, callExprs);
    for (clang::CallExpr *callexpr : callExprs) {
        if (bodyRange.isOutsideRange(callexpr))
            continue;

        clang::FunctionDecl *fDecl = callexpr->getDirectCallee();
        if (!fDecl)
            continue;

        if (fDecl->getNameAsString() == "setupUi")
            return callexpr;
    }

    return nullptr;
}

clang::FieldDecl *AutoConnectSlot::getClassMember(clang::CXXRecordDecl *record, const std::string &memberName)
{
    if (!record)
        return nullptr;

    for (auto field : record->fields()) {
        if (field->getNameAsString() == memberName)
            return field;
    }

    // Also include the base classes
    for (const auto &base : record->bases()) {
        clang::CXXRecordDecl *baseRecord = TypeUtils::recordFromBaseSpecifier(base);
        if (clang::FieldDecl *field = getClassMember(baseRecord, memberName))
            return field;
    }

    return nullptr;
}

const char *const s_checkName = "autoconnect-slot";
REGISTER_CHECK(s_checkName, AutoConnectSlot, CheckLevel1)
REGISTER_FIXIT(FixItConnects, "fix-autoconnect-slot", s_checkName)

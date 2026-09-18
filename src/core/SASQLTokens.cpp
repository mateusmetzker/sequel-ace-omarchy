//
//  SASQLTokens.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASQLTokens.h"

#include <QMutex>

#include "SPEditorTokens.h"

namespace SASQLTokens {

namespace {
QMutex &lexerMutex()
{
    // The flex scanner keeps global state; serialise access.
    static QMutex m;
    return m;
}
}

QVector<SASQLToken> tokenize(const QString &text, bool startInComment, bool *endsInComment)
{
    QVector<SASQLToken> tokens;
    const QByteArray utf8 = text.toUtf8();

    QMutexLocker lock(&lexerMutex());
    if (startInComment) SPEditorTokensScanInComment(utf8.constData(), static_cast<size_t>(utf8.size()));
    else SPEditorTokensScan(utf8.constData(), static_cast<size_t>(utf8.size()));

    int type;
    while ((type = SPEditorTokensNext()) != 0) {
        SASQLToken token;
        token.type = static_cast<SASQLTokenType>(type);
        token.start = static_cast<int>(SPEditorTokensOffset());
        token.length = static_cast<int>(SPEditorTokensLength());
        tokens.append(token);
    }
    if (endsInComment) *endsInComment = SPEditorTokensEndedInsideComment() != 0;
    return tokens;
}

QString uppercaseKeywords(const QString &text)
{
    QString result = text;
    const QVector<SASQLToken> tokens = tokenize(text);
    for (const SASQLToken &token : tokens) {
        if (token.type != SASQLTokenType::ReservedWord) continue;
        if (token.start < 0 || token.start + token.length > result.size()) continue;
        result.replace(token.start, token.length, result.mid(token.start, token.length).toUpper());
    }
    return result;
}

} // namespace SASQLTokens

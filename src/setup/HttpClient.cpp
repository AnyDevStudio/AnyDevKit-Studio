#include "HttpClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace Studio {

namespace {

constexpr int TransferTimeoutMilliseconds = 60000;

// One QNetworkReply behind the interface the tests can replace.
class NetworkTransfer final : public HttpTransfer {
public:
    NetworkTransfer(QNetworkReply* newReply, QObject* parent) : HttpTransfer(parent), reply(newReply) {
        reply->setParent(this);
        connect(reply, &QNetworkReply::readyRead, this, &HttpTransfer::dataReady);
        connect(reply, &QNetworkReply::finished, this, &HttpTransfer::finished);
    }

    int status() const override { return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); }

    qint64 rangeStart() const override {
        const auto match = QRegularExpression(R"(^bytes\s+(\d+)-)").match(QString::fromLatin1(reply->rawHeader("Content-Range")));
        return match.hasMatch() ? match.captured(1).toLongLong() : 0;
    }

    QString errorMessage() const override {
        // Statuses like 404 reach the caller through status(); only a broken transfer is an error here.
        if (reply->error() == QNetworkReply::NoError || status() >= 400) return {};
        return reply->errorString();
    }

    QByteArray takeData() override { return reply->readAll(); }
    void abort() override { reply->abort(); }

private:
    QNetworkReply* reply;
};

}

NetworkHttpClient::NetworkHttpClient() : manager(std::make_unique<QNetworkAccessManager>()) {}

NetworkHttpClient::~NetworkHttpClient() = default;

HttpTransfer* NetworkHttpClient::get(const QUrl& url, const QString& accept, qint64 rangeStart, QObject* parent) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(TransferTimeoutMilliseconds);
    request.setRawHeader("User-Agent", "AnyDevKit-Studio");
    request.setRawHeader("Accept", accept.toLatin1());
    if (rangeStart > 0) request.setRawHeader("Range", "bytes=" + QByteArray::number(rangeStart) + "-");
    return new NetworkTransfer(manager->get(request), parent);
}

}

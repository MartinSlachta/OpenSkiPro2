#pragma once

#include <QByteArray>

// Built-in test page of the API (GET /): the resources and the WebSocket messages live.
// A development aid, not a viewer for the public.
namespace ApiTestPage {
QByteArray html();
// Swagger UI page (GET /docs) on the embedded swagger-ui-dist, reading /api/v1/openapi.yaml.
QByteArray swaggerHtml();
}

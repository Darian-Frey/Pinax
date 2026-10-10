#pragma once

#include "domain/series_titles.h"

#include <QByteArray>
#include <QUrl>

#include <functional>
#include <string>
#include <vector>

namespace pinax::metadata {

class RequestQueue;

// Wikidata's query service, for the volumes of a series (F-030, D-031,
// SPEC.md §3.7). One SPARQL request: Wikidata's own search finds items
// named like the series, and the books "part of the series" (P179) of each
// come back with their number in it (P1545), authors and year. No key.
class WikidataClient {
public:
    explicit WikidataClient(RequestQueue& queue);

    // `credits` are the series' authors, to tell the owner's series from
    // others of the same name. A name with a prefix the owner added — "SW:
    // The New Jedi Order" — is asked again as what follows the colon.
    void findSeries(const std::string& name, const std::vector<std::string>& credits,
        std::function<void(domain::SeriesFind)> done);

    static QUrl seriesUrl(const std::string& name);

private:
    void ask(const std::string& name, const std::string& asked, const std::vector<std::string>& credits,
        std::function<void(domain::SeriesFind)> done);

    RequestQueue& queue_;
};

namespace wikidata {

// The SPARQL JSON results for seriesUrl(name), as one series: of the series
// found, the one that shares an author with `credits` (when any are known)
// and is named most like `name`, then the one with most volumes. Its books
// in order of their number, unnumbered ones after by year. An empty result
// when nothing fits.
domain::SeriesFind parseSeries(const QByteArray& json, const std::string& name,
    const std::vector<std::string>& credits);

} // namespace wikidata

} // namespace pinax::metadata

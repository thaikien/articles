#include <iostream>
#include <fstream>
#include <unordered_map>
#include <algorithm>

#include "graphs.hpp"

std::shared_ptr<graphs::graph> current_graph;
std::vector<std::shared_ptr<graphs::graph>> all_graphs;

void graphs::new_graph(const std::string& graph_name, const std::string& graph_title, const std::string& unit){
    current_graph = std::make_shared<graph>(graph_name, graph_title, unit);
    all_graphs.push_back(current_graph);

    std::cout << "Start " << graph_name << std::endl;
}

void graphs::new_result(const std::string& serie, const std::string& group, std::size_t value){
    current_graph->results.push_back({serie, group, value}); 

    std::cout << serie << ":" << group << ":" << value << std::endl;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> compute_values(std::shared_ptr<graphs::graph> graph){
    std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> results;

    for(auto& result : graph->results){
        results[result.group][result.serie] = result.value;
    }

    return results;
}

bool numeric_cmp(const std::string& lhs, const std::string& rhs){
    return atoi(lhs.c_str()) < atoi(rhs.c_str());
}

void graphs::output(Output output){
    if(output == Output::GOOGLE){
        std::ofstream file("graph.html");

        file << "<script type=\"text/javascript\">" << std::endl;

        //One function to rule them all
        for(auto& graph : all_graphs){
            file << "function draw_" << graph->name << "(){" << std::endl;

            file << "var data = google.visualization.arrayToDataTable([" << std::endl;
            
            //['x', 'Cats', 'Blanket 1', 'Blanket 2'],
            auto results = compute_values(graph);

            file << "['x'";

            auto first = results.begin()->second;
            for(auto& pair : first){
                file << ", '" << pair.first << "'";
            }

            file << "]," << std::endl;

            std::vector<std::string> groups;
            for(auto& group : results){
                groups.push_back(group.first);
            }
            std::sort(groups.begin(), groups.end(), numeric_cmp);

            std::size_t max = 0;
            for(auto& group_title : groups){
                file << "['" << group_title << "'";

                for(auto& serie : results[group_title]){
                    file << ", " << serie.second;
                    max = std::max(max, serie.second);
                }

                file << "]," << std::endl;
            }

            file << "]);" << std::endl;
        
            file << "var graph = new google.visualization.LineChart(document.getElementById('graph_" << graph->name <<"'));" << std::endl
                 << "var options = {curveType: \"function\","
                 << "title: \"" << graph->title << "\","
                 << "animation: {duration:1200, easing:\"in\"},"
                 << "width: 600, height: 400,"
                 << "hAxis: {title:\"Number of elements\", slantedText:true},"
                 << "vAxis: {viewWindow: {min:0}, title:\"" << graph->unit << "\"}};" << std::endl
                 << "graph.draw(data, options);" << std::endl;

            file << "var button = document.getElementById('graph_button_" << graph->name << "');" << std::endl
                 << "button.onclick = function(){" << std::endl
                 << "if(options.vAxis.logScale){" << std::endl
                 << "button.value=\"Logarithmic Scale\";" << std::endl
                 << "} else {" << std::endl
                 << "button.value=\"Normal scale\";" << std::endl
                 << "}" << std::endl
                 << "options.vAxis.logScale=!options.vAxis.logScale;" << std::endl
                 << "graph.draw(data, options);" << std::endl
                 << "};" << std::endl;

            file << "}" << std::endl;
        }

        //One function to find them
        file << "function draw_all(){" << std::endl;
        for(auto& graph : all_graphs){
            file << "draw_" << graph->name << "();" << std::endl;
        }
        file << "}" << std::endl;

        //One callback to bring them all
        file << "google.setOnLoadCallback(draw_all);" << std::endl;
        file << "</script>" << std::endl;
        file << std::endl;

        //And in the web page bind them
        for(auto& graph : all_graphs){
            file << "<div id=\"graph_" << graph->name << "\" style=\"width: 600px; height: 400px;\"></div>" << std::endl;
            file << "<input id=\"graph_button_" << graph->name << "\" type=\"button\" value=\"Logarithmic scale\">" << std::endl;
        }

        //...In the land of Google where shadow lies
    }
}

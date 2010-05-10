//-----------------------------------------------
// Copyright 2009 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// SGVisitors - Algorithms that visit
// each vertex in the graph and perform some
// operation
//
#include "SGVisitors.h"
#include "ErrorCorrect.h"

//
// SGFastaVisitor - output the vertices in the graph in 
// fasta format
//
bool SGFastaVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    m_fileHandle << ">" << pVertex->getID() << " " <<  pVertex->getSeq().length() 
                 << " " << pVertex->getReadCount() << "\n";
    m_fileHandle << pVertex->getSeq() << "\n";
    return false;
}


//
// SGOverlapWriterVisitor - write all the overlaps in the graph to a file 
//
bool SGOverlapWriterVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    EdgePtrVec edges = pVertex->getEdges();
    for(size_t i = 0; i < edges.size(); ++i)
    {
        Overlap ovr = edges[i]->getOverlap();
        if(ovr.id[0] < ovr.id[1])
            m_fileHandle << ovr << "\n";
    }
    return false;
}




//
// SGTransRedVisitor - Perform a transitive reduction about this vertex
// This uses Myers' algorithm (2005, The fragment assembly string graph)
// Precondition: the edge list is sorted by length (ascending)
void SGTransitiveReductionVisitor::previsit(StringGraph* pGraph)
{
    // The graph must not have containments
    assert(!pGraph->hasContainment());

    // Set all the vertices in the graph to "vacant"
    pGraph->setColors(GC_WHITE);
    pGraph->sortVertexAdjListsByLen();

    marked_verts = 0;
    marked_edges = 0;
}

bool SGTransitiveReductionVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    size_t trans_count = 0;
    static const size_t FUZZ = 10; // see myers...

    for(size_t idx = 0; idx < ED_COUNT; idx++)
    {
        EdgeDir dir = EDGE_DIRECTIONS[idx];
        EdgePtrVec edges = pVertex->getEdges(dir); // These edges are already sorted

        if(edges.size() == 0)
            continue;

        for(size_t i = 0; i < edges.size(); ++i)
            (edges[i])->getEnd()->setColor(GC_GRAY);

        Edge* pLongestEdge = edges.back();
        size_t longestLen = pLongestEdge->getSeqLen() + FUZZ;
        
        // Stage 1
        for(size_t i = 0; i < edges.size(); ++i)
        {
            Edge* pVWEdge = edges[i];
            Vertex* pWVert = pVWEdge->getEnd();

            //std::cout << "Examining edges from " << pWVert->getID() << " longest: " << longestLen << "\n";
            //std::cout << pWVert->getID() << " w_edges: \n";
            EdgeDir transDir = !pVWEdge->getTwinDir();
            if(pWVert->getColor() == GC_GRAY)
            {
                EdgePtrVec w_edges = pWVert->getEdges(transDir);
                for(size_t j = 0; j < w_edges.size(); ++j)
                {
                    Edge* pWXEdge = w_edges[j];
                    size_t trans_len = pVWEdge->getSeqLen() + pWXEdge->getSeqLen();
                    if(trans_len <= longestLen)
                    {
                        if(pWXEdge->getEnd()->getColor() == GC_GRAY)
                        {
                            // X is the endpoint of an edge of V, therefore it is transitive
                            pWXEdge->getEnd()->setColor(GC_BLACK);
                            //std::cout << "Marking " << pWXEdge->getEndID() << " as transitive to " << pVertex->getID() << "\n";
                        }
                    }
                    else
                        break;
                }
            }
        }
        
        // Stage 2
        for(size_t i = 0; i < edges.size(); ++i)
        {
            Edge* pVWEdge = edges[i];
            Vertex* pWVert = pVWEdge->getEnd();

            //std::cout << "Examining edges from " << pWVert->getID() << " longest: " << longestLen << "\n";
            //std::cout << pWVert->getID() << " w_edges: \n";
            EdgeDir transDir = !pVWEdge->getTwinDir();
            EdgePtrVec w_edges = pWVert->getEdges(transDir);
            for(size_t j = 0; j < w_edges.size(); ++j)
            {
                //std::cout << "    edge: " << *w_edges[j] << "\n";
                Edge* pWXEdge = w_edges[j];
                size_t len = pWXEdge->getSeqLen();

                if(len < FUZZ || j == 0)
                {
                    if(pWXEdge->getEnd()->getColor() == GC_GRAY)
                    {
                        // X is the endpoint of an edge of V, therefore it is transitive
                        pWXEdge->getEnd()->setColor(GC_BLACK);
                        //std::cout << "Marking " << pWXEdge->getEndID() << " as transitive to " << pVertex->getID() << " in stage 2";
                        //std::cout << " via " << pWVert->getID() << "\n";
                    }
                }
                else
                {
                    break;
                }
            }
        }

        for(size_t i = 0; i < edges.size(); ++i)
        {
            if(edges[i]->getEnd()->getColor() == GC_BLACK)
            {
                // Mark the edge and its twin for removal
                if(edges[i]->getColor() != GC_BLACK || edges[i]->getTwin()->getColor() != GC_BLACK)
                {
                    edges[i]->setColor(GC_BLACK);
                    edges[i]->getTwin()->setColor(GC_BLACK);
                    marked_edges += 2;
                    trans_count++;
                }
            }
            edges[i]->getEnd()->setColor(GC_WHITE);
        }
    }

    if(trans_count > 0)
        ++marked_verts;

    return false;
}

// Remove all the marked edges
void SGTransitiveReductionVisitor::postvisit(StringGraph* pGraph)
{
    printf("TR marked %d verts and %d edges\n", marked_verts, marked_edges);
    pGraph->sweepEdges(GC_BLACK);
    assert(pGraph->checkColors(GC_WHITE));
}

//
// SGContainRemoveVisitor - Removes contained
// vertices from the graph
//
void SGContainRemoveVisitor::previsit(StringGraph* pGraph)
{
    pGraph->setColors(GC_WHITE);
}

//
bool SGContainRemoveVisitor::visit(StringGraph* pGraph, Vertex* pVertex)
{
    // Skip the computation if this vertex has already been marked
    if(pVertex->getColor() == GC_BLACK)
        return false;

    EdgePtrVec edges = pVertex->getEdges();
    for(size_t i = 0; i < edges.size(); ++i)
    {
        Overlap ovr = edges[i]->getOverlap();
        Match m = edges[i]->getMatch();
        if(ovr.match.isContainment())
        {
            Vertex* pVertexY = edges[i]->getEnd();
            // Skip the resolution step if the vertex has already been marked
            if(pVertexY->getColor() == GC_BLACK)
                continue;

            Vertex* pToRemove = NULL;
            
            // If containedIdx is 0, then this vertex is the one to remove
            if(ovr.getContainedIdx() == 0)
            {
                pToRemove = pVertex;
            }
            else
            {
                pToRemove = pVertexY;
            }

            assert(pToRemove != NULL);
            pToRemove->setColor(GC_BLACK);


            // Add any new irreducible edges that exist when pToRemove is deleted
            // from the graph
            EdgePtrVec neighborEdges = pToRemove->getEdges();
            
            // This must be done in order of edge length or some transitive edges
            // may be created
            EdgeLenComp comp;
            std::sort(neighborEdges.begin(), neighborEdges.end(), comp);

            for(size_t j = 0; j < neighborEdges.size(); ++j)
            {
                Vertex* pRemodelVert = neighborEdges[j]->getEnd();
                Edge* pRemodelEdge = neighborEdges[j]->getTwin();
                SGAlgorithms::remodelVertexForExcision(pGraph, 
                                                       pRemodelVert, 
                                                       pRemodelEdge);
            }
            
            // Delete the edges from the graph
            for(size_t j = 0; j < neighborEdges.size(); ++j)
            {
                Vertex* pRemodelVert = neighborEdges[j]->getEnd();
                Edge* pRemodelEdge = neighborEdges[j]->getTwin();
                pRemodelVert->deleteEdge(pRemodelEdge);
                pToRemove->deleteEdge(neighborEdges[j]);
            }
        }
    }
    return false;
}

void SGContainRemoveVisitor::postvisit(StringGraph* pGraph)
{
    pGraph->sweepVertices(GC_BLACK);
    pGraph->setContainmentFlag(false);
}

//
// Validate the structure of the graph by detecting missing
// or erroneous edges
//
typedef std::pair<EdgeDesc, Overlap> EdgeDescOverlapPair;

// Comparator
struct EDOPairCompare
{
    bool operator()(const EdgeDescOverlapPair& edpXY, const EdgeDescOverlapPair& edpXZ)
    {
        return edpXY.second.match.coord[0].length() < edpXZ.second.match.coord[0].length();
    }
};

//
typedef std::priority_queue<EdgeDescOverlapPair, 
                            std::vector<EdgeDescOverlapPair>,
                            EDOPairCompare> EDOPairQueue;

// Simple getters for std::transform
EdgeDesc getEdgeDescFromEdge(Edge* pEdge)
{
    return pEdge->getDesc();
}

EdgeDesc getEdgeDescFromPair(const EdgeDescOverlapPair& pair)
{
    return pair.first;
}

// Print the elements of A that are not in B
void printSetDifference(const EdgeDescSet& a, const EdgeDescSet& b, const std::string& text)
{
    EdgeDescSet diff_set;
    std::insert_iterator<EdgeDescSet> diff_insert(diff_set, diff_set.begin());
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), diff_insert);

    if(!diff_set.empty())
    {
        std::cout << text << "\n";
        for(EdgeDescSet::iterator iter = diff_set.begin(); iter != diff_set.end(); ++iter)
        {
            std::cout << "\t" << *iter << "\n";
        }

        std::cout << " A set: " << "\n";
        for(EdgeDescSet::iterator iter = a.begin(); iter != a.end(); ++iter)
        {
            std::cout << "\t" << *iter << "\n";
        }

    }
}

//
bool SGValidateStructureVisitor::visit(StringGraph* pGraph, Vertex* pVertex)
{
    // Construct the complete set of potential overlaps for this vertex
    SGAlgorithms::EdgeDescOverlapMap overlapMap;
    SGAlgorithms::findOverlapMap(pVertex, pGraph->getErrorRate(), pGraph->getMinOverlap(), overlapMap);

    //std::cout << "Processing: " << pVertex->getID() << "\n";
    // Remove transitive overlaps from the overlap map
    EDOPairQueue overlapQueue;
    for(SGAlgorithms::EdgeDescOverlapMap::iterator iter = overlapMap.begin();
        iter != overlapMap.end(); ++iter)
    {
        overlapQueue.push(std::make_pair(iter->first, iter->second));
    }

    // Traverse the list of overlaps in order of length and remove elements from
    // the overlapMap if they are transitive
    while(!overlapQueue.empty())
    {
        EdgeDescOverlapPair edoPair = overlapQueue.top();
        overlapQueue.pop();

        EdgeDesc& edXY = edoPair.first;
        Overlap& ovrXY = edoPair.second;

        //std::cout << "CurrIR: " << ovrXY << " len: " << ovrXY.getOverlapLength(0) << "\n";
        
        SGAlgorithms::EdgeDescOverlapMap::iterator iter = overlapMap.begin();
        while(iter != overlapMap.end())
        {
            bool erase = false;
            const EdgeDesc& edXZ = iter->first;
            const Overlap& ovrXZ = iter->second;

            // Skip the self-match and any edges in the wrong direction
            if(!(edXZ == edXY) && edXY.dir == edXZ.dir && ovrXY.getOverlapLength(0) > ovrXZ.getOverlapLength(0))
            {
                // Infer the YZ overlap
                Overlap ovrYX = ovrXY;
                ovrYX.swap();
                Overlap ovrYZ = SGAlgorithms::inferTransitiveOverlap(ovrYX, ovrXZ);

                // Compute the error rate between the sequences
                double error_rate = SGAlgorithms::calcErrorRate(edXY.pVertex, edXZ.pVertex, ovrYZ);
                
                //std::cout << "\tOVRXY: " << ovrXY << "\n";
                //std::cout << "\tOVRXZ: " << ovrXZ << "\n";
                //std::cout << "\tOVRYZ: " << ovrYZ << " er: " << error_rate << "\n";
                
                if(isErrorRateAcceptable(error_rate, pGraph->getErrorRate()) && 
                   ovrYZ.getOverlapLength(0) >= pGraph->getMinOverlap())
                {
                    erase = true;
                }
            }
            
            if(erase)
                overlapMap.erase(iter++);
            else
                ++iter;
        }
    }

    // The edges remaining in the overlapMap are irreducible wrt pVertex
    // Compare the set of actual edges to the validation set
    EdgePtrVec edges = pVertex->getEdges();

    EdgeDescSet actual_set;
    std::insert_iterator<EdgeDescSet> actual_insert(actual_set, actual_set.begin());
    std::transform(edges.begin(), edges.end(), actual_insert, getEdgeDescFromEdge);

    EdgeDescSet validation_set;
    std::insert_iterator<EdgeDescSet> validation_insert(validation_set, validation_set.begin());
    std::transform(overlapMap.begin(), overlapMap.end(), validation_insert, getEdgeDescFromPair);

    std::stringstream ss_missing;
    ss_missing << pVertex->getID() << " has missing edges:";

    std::stringstream ss_extra;
    ss_extra << pVertex->getID() << " has extra edges:";

    printSetDifference(validation_set, actual_set, ss_missing.str());
    printSetDifference(actual_set, validation_set, ss_extra.str()); 

    return false;
}

//
// SGRemodelVisitor - Remodel the graph to infer missing edges or remove erroneous edges
//
void SGRemodelVisitor::previsit(StringGraph* pGraph)
{
    pGraph->setColors(GC_WHITE);
}

bool SGRemodelVisitor::visit(StringGraph* pGraph, Vertex* pVertex)
{
    bool graph_changed = false;
    (void)pGraph;
    for(size_t idx = 0; idx < ED_COUNT; idx++)
    {
        EdgeDir dir = EDGE_DIRECTIONS[idx];
        if(pVertex->countEdges(dir) > 1)
        {
            MultiOverlap mo = pVertex->getMultiOverlap();
            std::cout << "Primary MO: \n";
            mo.print();
            std::cout << "\nPrimary masked\n";
            mo.printMasked();
            
            EdgePtrVec edges = pVertex->getEdges(dir);
            for(size_t i = 0; i < edges.size(); ++i)
            {
                Edge* pXY = edges[i];
                Vertex* pY = pXY->getEnd();
                EdgeDir forwardDir = pXY->getTransitiveDir();
                EdgeDir backDir = !forwardDir;

                EdgePtrVec y_fwd_edges = pY->getEdges(forwardDir);
                EdgePtrVec y_back_edges = pY->getEdges(backDir);
                std::cout << pY->getID() << " forward edges: ";
                for(size_t j = 0; j < y_fwd_edges.size(); ++j)
                    std::cout << y_fwd_edges[j]->getEndID() << ",";
                std::cout << "\n";
                
                std::cout << pY->getID() << " back edges: ";
                for(size_t j = 0; j < y_back_edges.size(); ++j)
                    std::cout << y_back_edges[j]->getEndID() << ",";
                std::cout << "\n";
                std::cout << pY->getID() << " label " << pXY->getLabel() << "\n";
            }

            MultiOverlap extendedMO = SGAlgorithms::makeExtendedMultiOverlap(pVertex);
            std::cout << "\nExtended MO: \n";
            extendedMO.printMasked();

            ErrorCorrect::correctVertex(pVertex, 3, 0.01);
        }
    }
    return graph_changed;
}

//
void SGRemodelVisitor::postvisit(StringGraph*)
{
}

//
// SGErrorCorrectVisitor - Run error correction on the reads
//
bool SGErrorCorrectVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    static size_t numCorrected = 0;

    if(numCorrected > 0 && numCorrected % 50000 == 0)
        std::cerr << "Corrected " << numCorrected << " reads\n";

    std::string corrected = ErrorCorrect::correctVertex(pVertex, 5, 0.01);
    pVertex->setSeq(corrected);
    ++numCorrected;
    return false;
}

//
// SGEdgeStatsVisitor - Compute and display summary statistics of 
// the overlaps in the graph, including edges that were potentially missed
//
void SGEdgeStatsVisitor::previsit(StringGraph* pGraph)
{
    pGraph->setColors(GC_WHITE);
    maxDiff = 0;
    minOverlap = pGraph->getMinOverlap();
    maxOverlap = 0;

}

bool SGEdgeStatsVisitor::visit(StringGraph* pGraph, Vertex* pVertex)
{
    const int MIN_OVERLAP = pGraph->getMinOverlap();
    const double MAX_ERROR = pGraph->getErrorRate();

    static int visited = 0;
    ++visited;
    if(visited % 50000 == 0)
        std::cout << "visited: " << visited << "\n";

    // Add stats for the found overlaps
    EdgePtrVec edges = pVertex->getEdges();
    for(size_t i = 0; i < edges.size(); ++i)
    {
        Overlap ovr = edges[i]->getOverlap();
        int numDiff = ovr.match.countDifferences(pVertex->getSeq(), edges[i]->getEnd()->getSeq());
        int overlapLen = ovr.match.getMinOverlapLength();
        addOverlapToCount(overlapLen, numDiff, foundCounts);
    }
        
    // Explore the neighborhood around this graph for potentially missing overlaps
    CandidateVector candidates = getMissingCandidates(pGraph, pVertex, MIN_OVERLAP);
    MultiOverlap addedMO(pVertex->getID(), pVertex->getSeq());
    for(size_t i = 0; i < candidates.size(); ++i)
    {
        Candidate& c = candidates[i];
        int numDiff = c.ovr.match.countDifferences(pVertex->getSeq(), c.pEndpoint->getSeq());
        double error_rate = double(numDiff) / double(c.ovr.match.getMinOverlapLength());

        if(error_rate < MAX_ERROR)
        {
            int overlapLen = c.ovr.match.getMinOverlapLength();
            addOverlapToCount(overlapLen, numDiff, missingCounts);
        }
    }
    
    return false;
}

//
void SGEdgeStatsVisitor::postvisit(StringGraph* /*pGraph*/)
{    
    printf("FoundOverlaps\n");
    printCounts(foundCounts);

    printf("\nPotentially Missing Overlaps\n\n");
    printCounts(missingCounts);
}

//
void SGEdgeStatsVisitor::printCounts(CountMatrix& matrix)
{
    // Header row
    printf("OL\t");
    for(int j = 0; j <= maxDiff; ++j)
    {
        printf("%d\t", j);
    }

    printf("sum\n");
    IntIntMap columnTotal;
    for(int i = minOverlap; i <= maxOverlap; ++i)
    {
        printf("%d\t", i);
        int sum = 0;
        for(int j = 0; j <= maxDiff; ++j)
        {
            int v = matrix[i][j];
            printf("%d\t", v);
            sum += v;
            columnTotal[j] += v;
        }
        printf("%d\n", sum);
    }

    printf("total\t");
    int total = 0;
    for(int j = 0; j <= maxDiff; ++j)
    {
        int v = columnTotal[j];
        printf("%d\t", v);
        total += v;
    }
    printf("%d\n", total);
}

//
void SGEdgeStatsVisitor::addOverlapToCount(int ol, int nd, CountMatrix& matrix)
{
    matrix[ol][nd]++;

    if(nd > maxDiff)
        maxDiff = nd;

    if(ol > maxOverlap)
        maxOverlap = ol;
}

// Explore the neighborhood around a vertex looking for missing overlaps
SGEdgeStatsVisitor::CandidateVector SGEdgeStatsVisitor::getMissingCandidates(StringGraph* /*pGraph*/, 
                                                                             Vertex* pVertex, 
                                                                             int minOverlap) const
{
    CandidateVector out;

    // Mark the vertices that are reached from this vertex as black to indicate
    // they already are overlapping
    EdgePtrVec edges = pVertex->getEdges();
    for(size_t i = 0; i < edges.size(); ++i)
    {
        edges[i]->getEnd()->setColor(GC_BLACK);
    }
    pVertex->setColor(GC_BLACK);

    for(size_t i = 0; i < edges.size(); ++i)
    {
        Edge* pXY = edges[i];
        EdgePtrVec neighborEdges = pXY->getEnd()->getEdges();
        for(size_t j = 0; j < neighborEdges.size(); ++j)
        {
            Edge* pYZ = neighborEdges[j];
            if(pYZ->getEnd()->getColor() != GC_BLACK)
            {
                // Infer the overlap object from the edges
                Overlap ovrXY = pXY->getOverlap();
                Overlap ovrYZ = pYZ->getOverlap();

                if(SGAlgorithms::hasTransitiveOverlap(ovrXY, ovrYZ))
                {
                    Overlap ovr_xz = SGAlgorithms::inferTransitiveOverlap(ovrXY, ovrYZ);
                    if(ovr_xz.match.getMinOverlapLength() >= minOverlap)
                    {
                        out.push_back(Candidate(pYZ->getEnd(), ovr_xz));
                        pYZ->getEnd()->setColor(GC_BLACK);
                    }
                }
            }
        }
    }

    // Reset colors
    for(size_t i = 0; i < edges.size(); ++i)
        edges[i]->getEnd()->setColor(GC_WHITE);
    pVertex->setColor(GC_WHITE);
    for(size_t i = 0; i < out.size(); ++i)
        out[i].pEndpoint->setColor(GC_WHITE);
    return out;
}

//
// SGTrimVisitor - Remove "dead-end" vertices from the graph
//
void SGTrimVisitor::previsit(StringGraph* pGraph)
{
    num_island = 0;
    num_terminal = 0;
    num_contig = 0;
    pGraph->setColors(GC_WHITE);
}

// Mark any nodes that either dont have edges or edges in only one direction for removal
bool SGTrimVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    bool noext[2] = {0,0};

    for(size_t idx = 0; idx < ED_COUNT; idx++)
    {
        EdgeDir dir = EDGE_DIRECTIONS[idx];
        if(pVertex->countEdges(dir) == 0)
        {
            //std::cout << "Found terminal: " << pVertex->getID() << "\n";
            pVertex->setColor(GC_BLACK);
            noext[idx] = 1;
        }
    }

    if(noext[0] && noext[1])
        num_island++;
    else if(noext[0] || noext[1])
        num_terminal++;
    else
        num_contig++;
    return noext[0] || noext[1];
}

// Remove all the marked edges
void SGTrimVisitor::postvisit(StringGraph* pGraph)
{
    pGraph->sweepVertices(GC_BLACK);
    printf("island: %d terminal: %d contig: %d\n", num_island, num_terminal, num_contig);
}

//
// SGDuplicateVisitor - Detect and remove duplicate edges
//
void SGDuplicateVisitor::previsit(StringGraph* pGraph)
{
    pGraph->setColors(GC_WHITE);
}

bool SGDuplicateVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    pVertex->makeUnique();
    return false;
}

//
// SGIslandVisitor - Remove island (unconnected) vertices
//
void SGIslandVisitor::previsit(StringGraph* pGraph)
{
    pGraph->setColors(GC_WHITE);
}

// Mark any nodes that dont have edges
bool SGIslandVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    if(pVertex->countEdges() == 0)
    {
        pVertex->setColor(GC_BLACK);
        return true;
    }
    return false;
}

// Remove all the marked vertices
void SGIslandVisitor::postvisit(StringGraph* pGraph)
{
    pGraph->sweepVertices(GC_BLACK);
}


//
// SGBubbleVisitor - Find and collapse variant
// "bubbles" in the graph
//
void SGBubbleVisitor::previsit(StringGraph* pGraph)
{
    pGraph->setColors(GC_WHITE);
    num_bubbles = 0;
}

// Find bubbles (nodes where there is a split and then immediate rejoin) and mark them for removal
bool SGBubbleVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    bool bubble_found = false;
    for(size_t idx = 0; idx < ED_COUNT; idx++)
    {
        EdgeDir dir = EDGE_DIRECTIONS[idx];
        EdgePtrVec edges = pVertex->getEdges(dir);
        if(edges.size() > 1)
        {
            // Check the vertices
            for(size_t i = 0; i < edges.size(); ++i)
            {
                Edge* pVWEdge = edges[i];
                Vertex* pWVert = pVWEdge->getEnd();

                // Get the edges from w in the same direction
                EdgeDir transDir = !pVWEdge->getTwinDir();
                EdgePtrVec wEdges = pWVert->getEdges(transDir);

                if(pWVert->getColor() == GC_RED)
                    return false;

                // If the bubble has collapsed, there should only be one edge
                if(wEdges.size() == 1)
                {
                    Vertex* pBubbleEnd = wEdges.front()->getEnd();
                    if(pBubbleEnd->getColor() == GC_RED)
                        return false;
                }
            }

            // Mark the vertices
            for(size_t i = 0; i < edges.size(); ++i)
            {
                Edge* pVWEdge = edges[i];
                Vertex* pWVert = pVWEdge->getEnd();

                // Get the edges from w in the same direction
                EdgeDir transDir = !pVWEdge->getTwinDir();
                EdgePtrVec wEdges = pWVert->getEdges(transDir);

                // If the bubble has collapsed, there should only be one edge
                if(wEdges.size() == 1)
                {
                    Vertex* pBubbleEnd = wEdges.front()->getEnd();
                    if(pBubbleEnd->getColor() == GC_BLACK)
                    {
                        // The endpoint has been visited, set this vertex as needing removal
                        // and set the endpoint as unvisited
                        pWVert->setColor(GC_RED);
                        bubble_found = true;
                    }
                    else
                    {
                        pBubbleEnd->setColor(GC_BLACK);
                        pWVert->setColor(GC_BLUE);
                    }
                }
            }
            
            // Unmark vertices
            for(size_t i = 0; i < edges.size(); ++i)
            {
                Edge* pVWEdge = edges[i];
                Vertex* pWVert = pVWEdge->getEnd();

                // Get the edges from w in the same direction
                EdgeDir transDir = !pVWEdge->getTwinDir();
                EdgePtrVec wEdges = pWVert->getEdges(transDir);

                // If the bubble has collapsed, there should only be one edge
                if(wEdges.size() == 1)
                {
                    Vertex* pBubbleEnd = wEdges.front()->getEnd();
                    pBubbleEnd->setColor(GC_WHITE);
                }
                if(pWVert->getColor() == GC_BLUE)
                    pWVert->setColor(GC_WHITE);
            }

            if(bubble_found)
                ++num_bubbles;

        }
    }
    return bubble_found;
}

// Remove all the marked edges
void SGBubbleVisitor::postvisit(StringGraph* pGraph)
{
    pGraph->sweepVertices(GC_RED);
    printf("bubbles: %d\n", num_bubbles);
    assert(pGraph->checkColors(GC_WHITE));
}

//
// SGGraphStatsVisitor - Collect summary stasitics
// about the graph
//
void SGGraphStatsVisitor::previsit(StringGraph* /*pGraph*/)
{
    num_terminal = 0;
    num_island = 0;
    num_monobranch = 0;
    num_dibranch = 0;
    num_transitive = 0;
    num_edges = 0;
    num_vertex = 0;
    sum_edgeLen = 0;
}

// Find bubbles (nodes where there is a split and then immediate rejoin) and mark them for removal
bool SGGraphStatsVisitor::visit(StringGraph* /*pGraph*/, Vertex* pVertex)
{
    int s_count = pVertex->countEdges(ED_SENSE);
    int as_count = pVertex->countEdges(ED_ANTISENSE);
    if(s_count == 0 && as_count == 0)
    {
        ++num_island;
    }
    else if(s_count == 0 || as_count == 0)
    {
        ++num_terminal;
    }

    if(s_count > 1 && as_count > 1)
        ++num_dibranch;
    else if(s_count > 1 || as_count > 1)
        ++num_monobranch;

    if(s_count == 1 || as_count == 1)
        ++num_transitive;

    num_edges += (s_count + as_count);
    ++num_vertex;

    EdgePtrVec edges = pVertex->getEdges();
    for(size_t i = 0; i < edges.size(); ++i)
        sum_edgeLen += edges[i]->getSeqLen();

    return false;
}

// Remove all the marked edges
void SGGraphStatsVisitor::postvisit(StringGraph* /*pGraph*/)
{
    printf("island: %d terminal: %d monobranch: %d dibranch: %d transitive: %d\n", num_island, num_terminal,
                                                                                   num_monobranch, num_dibranch, num_transitive);
    printf("Total Vertices: %d Total Edges: %d Sum edge length: %zu\n", num_vertex, num_edges, sum_edgeLen);
}

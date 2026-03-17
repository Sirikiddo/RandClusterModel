import matplotlib.pyplot as plt
import networkx as nx

class GraphView():
    def __init__(self, pointList, edgeDict, keyList, labelList, drawArea, pointsWithMessage, targetPoints):
        self.G = nx.Graph()

        self.nodes = [i for i in range(len(pointList))]
        self.pointList = pointList

        self.G.add_nodes_from(self.nodes)

        #fig, self.ax = plt.subplots()
        #fig.subplots_adjust(left=0.25, bottom=0.25)

        self.keyList = keyList
        self.edgeDict = edgeDict
        self.labelList = labelList

        for key in self.keyList:
            p = self.edgeDict[key]
            self.G.add_edge(key[0], key[1], weight = p)

        self.elarge = [(u, v) for (u, v, d) in self.G.edges(data=True) if d["weight"] > 0.5]
        self.esmall = [(u, v) for (u, v, d) in self.G.edges(data=True) if d["weight"] <= 0.5]

        # draw nodes

        normalNodes = []
        for i in self.nodes:
            if i not in pointsWithMessage and i not in targetPoints:
                normalNodes.append(i)


        nx.draw_networkx_nodes(self.G, self.pointList, nodelist=targetPoints, node_color="tab:red", node_size=400)
        nx.draw_networkx_nodes(self.G, self.pointList, nodelist=normalNodes, node_color="tab:blue", node_size=400)
        nx.draw_networkx_nodes(self.G, self.pointList, nodelist=pointsWithMessage, node_color="tab:green", node_size=400)

        # draw edges
        nx.draw_networkx_edges(self.G, self.pointList, edgelist=self.elarge, width=3)
        nx.draw_networkx_edges(
            self.G, self.pointList, edgelist=self.esmall, width=3, alpha=0.5, edge_color="b", style="dashed"
        )

        # node labels
        nodeLables = {}
        for i in range(len(self.labelList)):
            #nodeLables[i] = str(self.labelList[i])
            nodeLables[i] = str(i)


        nx.draw_networkx_labels(self.G, self.pointList, nodeLables, font_size=20, font_family="sans-serif")
        # edge weight labels whith prescision
        edge_labels = dict([((u,v,), f"{d['weight']:.2f}") for u,v,d in self.G.edges(data=True)])
        nx.draw_networkx_edge_labels(self.G, self.pointList, edge_labels, font_size=9)

        self.ax = plt.gca()
        self.ax.set_xlim(drawArea[0][0], drawArea[0][1])
        self.ax.set_ylim(drawArea[1][0], drawArea[1][1])
        self.ax.set_aspect("equal", 'box')
        
        self.ax.margins(0.08)
        plt.axis("off")
        plt.tight_layout()
        #plt.show()

    def update(self, pointsWithMessage, targetPoints):
        self.ax.cla()

        self.G = nx.Graph()
        self.G.add_nodes_from(self.nodes)

        for key in self.keyList:
            p = self.edgeDict[key]
            self.G.add_edge(key[0], key[1], weight = p)
        
        # draw nodes
        normalNodes = []
        for i in self.nodes:
            if i not in pointsWithMessage and i not in targetPoints:
                normalNodes.append(i)

        nx.draw_networkx_nodes(self.G, self.pointList, nodelist=targetPoints, node_color="tab:red", node_size=400)
        nx.draw_networkx_nodes(self.G, self.pointList, nodelist=normalNodes, node_color="tab:blue", node_size=400)
        nx.draw_networkx_nodes(self.G, self.pointList, nodelist=pointsWithMessage, node_color="tab:green", node_size=400)

        # draw edges
        nx.draw_networkx_edges(self.G, self.pointList, edgelist=self.elarge, width=3)
        nx.draw_networkx_edges(
            self.G, self.pointList, edgelist=self.esmall, width=3, alpha=0.5, edge_color="b", style="dashed"
        )

        # node labels
        nodeLables = {}
        for i in range(len(self.labelList)):
            nodeLables[i] = str(self.labelList[i])


        nx.draw_networkx_labels(self.G, self.pointList, nodeLables, font_size=20, font_family="sans-serif")
        # edge weight labels whith prescision
        edge_labels = dict([((u,v,), f"{d['weight']:.2f}") for u,v,d in self.G.edges(data=True)])
        nx.draw_networkx_edge_labels(self.G, self.pointList, edge_labels, font_size=9)

        plt.show()

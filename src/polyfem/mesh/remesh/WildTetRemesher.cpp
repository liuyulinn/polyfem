#include "WildTetRemesher.hpp"

#include <polyfem/utils/GeometryUtils.hpp>

#include <wmtk/utils/TupleUtils.hpp>

#include <igl/predicates/predicates.h>
#include <igl/edges.h>

namespace polyfem::mesh
{
	/// @brief Construct a new WildTetRemesher object
	/// @param state Simulation current state
	WildTetRemesher::WildTetRemesher(
		const State &state,
		const Eigen::MatrixXd &obstacle_displacements,
		const Eigen::MatrixXd &obstacle_vals,
		const double current_time,
		const double starting_energy)
		: Super(state, obstacle_displacements, obstacle_vals, current_time, starting_energy)
	{
	}

	void WildTetRemesher::init_attributes_and_connectivity(
		const size_t num_vertices, const Eigen::MatrixXi &tetrahedra)
	{
		// Register attributes
		p_vertex_attrs = &vertex_attrs;
		p_edge_attrs = &edge_attrs;
		p_face_attrs = &boundary_attrs;
		p_tet_attrs = &element_attrs;

		// Convert from eigen to internal representation
		std::vector<std::array<size_t, 4>> tets(tetrahedra.rows());
		for (int i = 0; i < tetrahedra.rows(); i++)
			for (int j = 0; j < tetrahedra.cols(); j++)
				tets[i][j] = (size_t)tetrahedra(i, j);

		// Initialize the trimesh class which handles connectivity
		wmtk::TetMesh::init(num_vertices, tets);
	}

	// split_edge_after in wild_remesh/Split.cpp

	Eigen::MatrixXi WildTetRemesher::boundary_edges() const
	{
		const Eigen::MatrixXi BF = boundary_faces();
		Eigen::MatrixXi BE;
		igl::edges(BF, BE);
		if (obstacle().n_edges() > 0)
			utils::append_rows(BE, obstacle().e().array() + vert_capacity());
		return BE;
	}

	Eigen::MatrixXi WildTetRemesher::boundary_faces() const
	{
		const std::vector<Tuple> faces = get_faces();
		int num_boundary_faces = 0;
		Eigen::MatrixXi BF(faces.size(), 3);
		for (int i = 0; i < faces.size(); ++i)
		{
			const Tuple &f = faces[i];
			if (f.switch_tetrahedron(*this).has_value()) // not a boundary face
				continue;
			const std::array<Tuple, 3> vs = get_face_vertices(f);
			BF(num_boundary_faces, 0) = vs[0].vid(*this);
			BF(num_boundary_faces, 1) = vs[1].vid(*this);
			BF(num_boundary_faces, 2) = vs[2].vid(*this);
			num_boundary_faces++;
		}
		BF.conservativeResize(num_boundary_faces, 3);
		if (obstacle().n_faces() > 0)
			utils::append_rows(BF, obstacle().f().array() + vert_capacity());
		return BF;
	}

	bool WildTetRemesher::is_inverted(const Tuple &loc) const
	{
		// Get the vertices ids
		const std::array<size_t, 4> vids = oriented_tet_vids(loc);

		igl::predicates::exactinit();

		for (int i = 0; i < n_quantities() / 3 + 2; ++i)
		{
			// Use igl for checking orientation
			const igl::predicates::Orientation orientation = igl::predicates::orient3d(
				vertex_attrs[vids[0]].position_i(i), vertex_attrs[vids[1]].position_i(i),
				vertex_attrs[vids[2]].position_i(i), vertex_attrs[vids[3]].position_i(i));

			// neg result == pos tet (tet origin from geogram delaunay)
			if (orientation != igl::predicates::Orientation::NEGATIVE)
				return true;
		}

		return false;
	}

	double WildTetRemesher::element_volume(const Tuple &e) const
	{
		const std::array<size_t, 4> vids = oriented_tet_vids(e);
		return utils::tetrahedron_volume(
			vertex_attrs[vids[0]].rest_position,
			vertex_attrs[vids[1]].rest_position,
			vertex_attrs[vids[2]].rest_position,
			vertex_attrs[vids[3]].rest_position);
	}

	std::vector<WildTetRemesher::Tuple> WildTetRemesher::boundary_facets() const
	{
		std::vector<Tuple> boundary_faces;
		for (const Tuple &f : get_faces())
			if (!f.switch_tetrahedron(*this))
				boundary_faces.push_back(f);
		return boundary_faces;
	}

	// map_edge_split_boundary_attributes/map_edge_split_element_attributes in wild_remesh/Split.cpp

} // namespace polyfem::mesh
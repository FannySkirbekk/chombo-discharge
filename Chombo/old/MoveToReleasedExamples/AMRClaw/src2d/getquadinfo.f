      subroutine get_aux_locations_n(ixy,mcapa,locrot,locarea)
      implicit none

      integer ixy, mcapa, locrot, locarea

c     # Get aux locations for normal solve
      mcapa = 7
      locrot = (ixy-1)*3 + 1
      locarea = locrot + 2
      end

      subroutine get_aux_locations(ixy,mcapa, locrot,locarea)
      implicit none

      integer ixy, mcapa, locrot, locarea, icoor

c     # Get aux locations for transverse solves.

c     # Get transverse direction
      icoor = 3 - ixy

      call get_aux_locations_n(icoor,mcapa,locrot,locarea)

      end

      subroutine compute_tangent(rot)
      implicit none

      double precision rot(4)

      rot(3) = -rot(2)
      rot(4) = rot(1)

      end

      subroutine rotate2(rot,velcomps)
      implicit none

      double precision rot(4), velcomps(2), v1, v2

      v1 = velcomps(1)
      v2 = velcomps(2)

      velcomps(1) = rot(1)*v1 + rot(2)*v2
      velcomps(2) = rot(3)*v1 + rot(4)*v2

      end

      subroutine rotate2_tr(rot,velcomps)
      implicit none

      double precision velcomps(2), rot(4), v1, v2

      v1 = velcomps(1)
      v2 = velcomps(2)

      velcomps(1) = rot(1)*v1 + rot(3)*v2
      velcomps(2) = rot(2)*v1 + rot(4)*v2

      end

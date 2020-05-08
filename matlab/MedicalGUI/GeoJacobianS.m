function JstS = GeoJacobianS(twists, joint_pos)
    %#codegen
    %GeoJacobianS
    JstS = twists;
    PoE = screw2tform(twists(:,1), joint_pos(1));
    for i = 2:size(twists,2)
        JstS(:,i) = tform2adjoint(PoE)*twists(:,i);
        PoE = PoE*screw2tform(twists(:,i), joint_pos(i));
    end
end


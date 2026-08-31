#include "project_repository.h"

namespace dw {

ProjectRecord ProjectRepository::rowToProject(Statement& statement) {
    ProjectRecord project;
    project.id = statement.getInt(0);
    project.name = statement.getText(1);
    project.description = statement.getText(2);
    project.filePath = statement.getText(3);
    project.notes = statement.getText(4);
    project.createdAt = statement.getText(5);
    project.modifiedAt = statement.getText(6);
    project.temporary = statement.getInt(7) != 0;
    return project;
}

} // namespace dw
